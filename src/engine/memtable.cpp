/**
 * VaultDB — MemTable Implementation
 *
 * The MemTable uses std::map (red-black tree) to keep keys sorted.
 * Each insert is O(log n), but the payoff is that flushing to SSTable
 * is a simple sequential iteration — O(n) instead of O(n log n).
 */

#include "memtable.h"

namespace vaultdb {

MemTable::MemTable(size_t max_size_bytes)
    : max_size_bytes_(max_size_bytes), current_size_bytes_(0) {}

void MemTable::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = data_.find(key);
    if (it != data_.end()) {
        // Update existing: subtract old size, add new
        current_size_bytes_ -= (it->first.size() + it->second.size());
    }

    data_[key] = value;
    current_size_bytes_ += (key.size() + value.size());
}

std::optional<std::string> MemTable::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    return it->second;
}

void MemTable::del(const std::string& key) {
    // Delete = write a tombstone marker (not a real deletion)
    // Tombstones propagate to SSTables and are cleaned during compaction
    set(key, TOMBSTONE);
}

bool MemTable::is_full() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_size_bytes_ >= max_size_bytes_;
}

size_t MemTable::size_bytes() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_size_bytes_;
}

size_t MemTable::count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.size();
}

std::vector<std::pair<std::string, std::string>> MemTable::get_sorted_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    // std::map is already sorted, so this is a simple O(n) copy
    return std::vector<std::pair<std::string, std::string>>(
        data_.begin(), data_.end());
}

void MemTable::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
    current_size_bytes_ = 0;
}

}  // namespace vaultdb
