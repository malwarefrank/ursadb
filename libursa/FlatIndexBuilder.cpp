#include "FlatIndexBuilder.h"

#include <algorithm>
#include <fstream>

#include "RawFile.h"
#include "Utils.h"

// raw_data will occupy at most 512 MB (MAX_TRIGRAMS*8 bytes)
// TODO(): this should be parametrised by user somehow.
constexpr int MAX_TRIGRAMS = 1024 * 1024 * 64;

FlatIndexBuilder::FlatIndexBuilder(IndexType ntype)
    : IndexBuilder(ntype), max_fileid_(0) {
    raw_data.reserve(MAX_TRIGRAMS);
}

void FlatIndexBuilder::add_trigram(FileId fid, TriGram val) {
    raw_data.push_back(fid | (uint64_t{val} << 40ULL));
}

// Countsort operating on bytes. Stable sort by (x) => (x>>shift) & 0xFF.
void countsort(std::vector<uint64_t> *data_v, std::vector<uint64_t> *swap_v,
               int shift) {
    size_t count[256] = {};
    int64_t n = data_v->size();
    uint64_t *data = data_v->data();
    uint64_t *swap = swap_v->data();

    if (data_v->size() != swap_v->size()) {
        throw std::runtime_error("Swap space size doesn't match the data");
    }

    for (int64_t i = 0; i < n; i++) {
        count[(data[i] >> shift) & 0xFF]++;
    }

    for (int64_t i = 1; i < 256; i++) {
        count[i] += count[i - 1];
    }

    for (int64_t i = n - 1; i >= 0; i--) {
        swap[count[(data[i] >> shift) & 0xFF] - 1] = data[i];
        count[(data[i] >> shift) & 0xFF]--;
    }

    data_v->swap(*swap_v);
}

// Count number of bits in integer. Caveat: this will return 0 for 0.
int count_bytes(uint32_t integer) {
    int bytes = 0;
    while (integer > 0) {
        integer >>= 8;
        bytes++;
    }
    return bytes;
}

// Radixsort, optimised for our data format. Core of the code is a standard
// radix sort, with the following optimisations:
// 1. We know that all records in data have a following format:
// [TTT][FFFFF] maximum fileid won't be anyware close to 5 bytes, so we can
//   skip sorting 4th, 5th, probably 6th and maybe even 7th byte.
// 2. Counting sort needs a temporary storage space. Instead of doing a copy
//   every time, use two vectors and swap their data.
void flat_radixsort(std::vector<uint64_t> *data, uint64_t max_fileid) {
    std::vector<uint64_t> swap(data->size());
    int skip_to = count_bytes(max_fileid) * 8;
    for (int shift = 0; shift < 64; shift += 8) {
        if (shift >= skip_to && shift < 40) {
            continue;
        }
        countsort(data, &swap, shift);
    }
}

void FlatIndexBuilder::save(const std::string &fname) {
    RawFile fd(fname, O_WRONLY | O_CREAT | O_EXCL, 0600);

    uint32_t magic = DB_MAGIC;
    uint32_t version = 6;
    auto ndx_type = static_cast<uint32_t>(index_type());
    uint32_t reserved = 0;

    fd.write<uint32_t>(&magic, 1);
    fd.write<uint32_t>(&version, 1);
    fd.write<uint32_t>(&ndx_type, 1);
    fd.write<uint32_t>(&reserved, 1);

    auto offset = 16;
    std::vector<uint64_t> offsets(NUM_TRIGRAMS + 1);
    offsets[0] = offset;
    // Sort raw_data by trigrams (higher part of raw_data contains the
    // trigram value and lower part has the file ID).
    // According to benchmarks, this is the slowest part of save() method.
    flat_radixsort(&raw_data, max_fileid_);

    // Remove the duplicates (Files will often contain duplicated trigrams).
    raw_data.erase(std::unique(raw_data.begin(), raw_data.end()),
                   raw_data.end());

    TriGram last_trigram = 0;
    PosixRunWriter writer(fd.get());
    for (uint64_t d : raw_data) {
        TriGram val = (d >> 40ULL) & 0xFFFFFFU;
        FileId next = d & 0xFFFFFFFFFFULL;

        // adjust offsets for [last_trigram+1, val)
        if (last_trigram != val) {
            offset += writer.bytes_written();
            for (TriGram v = last_trigram + 1; v <= val; v++) {
                offsets[v] = offset;
            }
            writer.reset();
            last_trigram = val;
        }

        writer.write(next);
    }
    offset += writer.bytes_written();
    writer.flush();

    for (TriGram v = last_trigram + 1; v <= NUM_TRIGRAMS; v++) {
        offsets[v] = offset;
    }

    fd.write(offsets.data(), (NUM_TRIGRAMS + 1));
}

void FlatIndexBuilder::add_file(FileId fid, const uint8_t *data, size_t size) {
    max_fileid_ = std::max(fid, max_fileid_);
    TrigramGenerator generator = get_generator_for(index_type());
    generator(data, size, [&](TriGram val) { add_trigram(fid, val); });
}

bool FlatIndexBuilder::can_still_add(uint64_t bytes,
                                     [[maybe_unused]] int file_count) const {
    uint64_t max_trigrams_produced = bytes < 3 ? 0 : bytes - 2;
    uint64_t max_trigrams_after_add = raw_data.size() + max_trigrams_produced;
    return max_trigrams_after_add < MAX_TRIGRAMS;
}
