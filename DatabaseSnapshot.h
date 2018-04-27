#pragma once

#include <experimental/filesystem>
#include <map>
#include <string>
#include <vector>
#include <random>
#include <iostream>

#include "Task.h"
#include "Query.h"

namespace fs = std::experimental::filesystem;

class OnDiskDataset;

class DatabaseSnapshot {
    fs::path db_name;
    fs::path db_base;
    std::vector<const OnDiskDataset*> datasets;
    const std::map<uint64_t, Task> *tasks;
    size_t max_memory_size;
    std::mt19937_64 random;

    std::string allocate_name();

public:
    DatabaseSnapshot(fs::path db_name, fs::path db_base, std::vector<const OnDiskDataset*> datasets,
                     const std::map<uint64_t, Task> *tasks, size_t max_memory_size);
    void index_path(Task *task, const std::vector<IndexType> types, const std::string &filepath);
    void execute(const Query &query, Task *task, std::vector<std::string> *out);
    void compact(Task *task);
    const std::vector<const OnDiskDataset*> &get_datasets() { return datasets; };
    const std::map<uint64_t, Task> *get_tasks() { return tasks; };
};
