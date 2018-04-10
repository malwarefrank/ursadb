#pragma once

#include <cstdint>
#include <cstddef>
#include "MemMap.h"

class MemMap {
    int fd;
    uint8_t *mmap_ptr;
    size_t fsize;

public:
    MemMap(const std::string &fname);
    ~MemMap();
    uint8_t &operator[](const size_t &offset);
    const size_t &size();
};
