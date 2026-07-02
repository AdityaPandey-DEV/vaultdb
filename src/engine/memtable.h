#pragma once
/**
 * VaultDB — MemTable (In-Memory Write Buffer)
 *
 * MemTable is the first stop for all writes. It's backed by std::map
 * (a red-black tree) which keeps keys sorted. This is important because
 * when we flush to SSTable, we need sorted data for efficient binary search.
 *
 * "MemTable is sorted so SSTable flush is just a sequential write — O(n)
 *  not O(n log n). The sorting cost is amortized across individual inserts
 *  at O(log n) each."
 *
 * When the MemTable exceeds the size threshold (default 4MB), the LSM engine
 * flushes it to a new SSTable file on disk.
 *
 * Special DELETE markers (tombstones): When a key is deleted, we don't remove
 * it from the MemTable. Instead, we store a special tombstone value. During
 * SSTable compaction, tombstones suppress older values of the same key.
 */

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vaultdb {

// Tombstone marker — indicates a deleted key
static const std::string TOMBSTONE = "__VAULTDB_TOMBSTONE__";

class MemTable {
public:
    /**
     * Create a MemTable with the given size threshold.
     * @param max_size_bytes Flush threshold in bytes (default: 4MB).
     */
    explicit MemTable(size_t max_size_bytes = 4 * 1024 * 1024)
        : max_size_bytes_(max_size_bytes), current_size_bytes_(0) {}

    /**
     * Set a key-value pair in the MemTable.
     * If the key already exists, it is updated (old size subtracted, new size added).
     *
     * @param key The key to set.
     * @param value The value to associate.
     *
     * Time: O(log n) — std::map insertion
     */
    void set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = data_.find(key);
        if (it != data_.end()) {
            // Update existing: subtract old size, add new
            current_size_bytes_ -= (it->first.size() + it->second.size());
        }

        data_[key] = value;
        current_size_bytes_ += (key.size() + value.size());
    }

    /**
     * Get the value for a key.
     *
     * @param key The key to look up.
     * @return The value if found (may be TOMBSTONE), std::nullopt if not present.
     *
     * Time: O(log n) — std::map lookup
     */
    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = data_.find(key);
        if (it == data_.end()) return std::nullopt;
        return it->second;
    }

    /**
     * Delete a key by writing a tombstone marker.
     * The tombstone is kept in the MemTable and flushed to SSTable.
     * During compaction, it suppresses older values.
     *
     * @param key The key to delete.
     */
    void del(const std::string& key) {
        set(key, TOMBSTONE);
    }

    /**
     * Check if the MemTable has exceeded its size threshold.
     * When true, the LSM engine should flush this to a new SSTable.
     */
    bool is_full() {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_size_bytes_ >= max_size_bytes_;
    }

    /**
     * Get the current size in bytes.
     */
    size_t size_bytes() {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_size_bytes_;
    }

    /**
     * Get the number of entries.
     */
    size_t count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    /**
     * Get all key-value pairs in sorted order (for SSTable flush).
     * @return Vector of (key, value) pairs sorted by key.
     */
    std::vector<std::pair<std::string, std::string>> get_sorted_entries() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<std::pair<std::string, std::string>>(
            data_.begin(), data_.end());
    }

    /**
     * Clear all entries (called after successful flush to SSTable).
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.clear();
        current_size_bytes_ = 0;
    }

private:
    std::map<std::string, std::string> data_;  // Sorted by key (red-black tree)
    std::mutex mutex_;
    size_t max_size_bytes_;
    size_t current_size_bytes_;
};

}  // namespace vaultdb
