#pragma once
#include "tools/tool.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

class ToolRegistry {
public:
    ToolRegistry();
    ~ToolRegistry();

    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    bool add(std::unique_ptr<Tool> tool);

    Tool* find(std::string_view name) const;

    std::size_t size() const noexcept;

    void for_each(const std::function<void(const Tool&)>& fn) const;

    std::size_t bucket_count() const noexcept;

    double load_factor() const noexcept;

    std::size_t max_probe_length() const;

    static std::size_t hash(std::string_view key) noexcept;

private:
    struct Entry {
        std::string name;
        std::unique_ptr<Tool> tool;
    };

    std::vector<std::vector<Entry>> buckets_;
    std::size_t size_ = 0;

    void rehash(std::size_t new_bucket_count);
};
