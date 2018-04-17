#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <stdexcept>

class MemMap {
    int fd;
    std::string fname;
    uint8_t *mmap_ptr;
    size_t fsize;

public:
    explicit MemMap(const std::string &fname);
    MemMap(MemMap &&other);
    ~MemMap();

    // Disables copy constructor - we DO NOT want to accidentaly copy MemMap object
    MemMap(const MemMap &other) = delete;

    const std::string &name() const {
        return fname;
    }

    const uint8_t *data() const {
        return mmap_ptr;
    }

    const size_t &size() const {
        return fsize;
    }
};

class empty_file_error : public std::runtime_error
{
    std::string what_message;
public:
    explicit empty_file_error(const std::string& __arg) : runtime_error(__arg) {}

    const char* what()
    {
        return what_message.c_str();
    }
};
