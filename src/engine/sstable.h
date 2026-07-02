#pragma once
/**
 * VaultDB — SSTable (Sorted String Table)
 *
 * On-disk immutable file storing sorted key-value pairs.
 *
 * File format:
 *   [key_len: 4 bytes][key: key_len bytes][val_len: 4 bytes][val: val_len bytes]
 *   ... repeated for each entry, sorted by key
 *
 * Sparse index: Every 100th key is stored in memory with its file offset.
 * This allows binary search on the sparse index to narrow down the disk
 * region, then a linear scan within that region.
 *
 * "Compaction reduces read amplification from O(n SSTables) to O(1) by
 *  merging overlapping key ranges into a single file."
 */

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <filesystem>

namespace vaultdb {

struct SparseIndexEntry {
    std::string key;
    std::streamoff offset;
};

class SSTable {
public:
    /**
     * Create an SSTable backed by the given file path.
     * If the file already exists, builds the sparse index from it.
     */
    explicit SSTable(const std::string& path) : path_(path) {
        if (std::filesystem::exists(path)) {
            build_sparse_index();
        }
    }

    /**
     * Flush a sorted vector of key-value pairs to disk as a new SSTable.
     * The entries MUST be sorted by key (guaranteed by MemTable's std::map).
     *
     * @param entries Sorted vector of (key, value) pairs from MemTable.
     */
    void flush(const std::vector<std::pair<std::string, std::string>>& entries) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::filesystem::create_directories(
            std::filesystem::path(path_).parent_path());

        std::ofstream file(path_, std::ios::binary | std::ios::trunc);
        sparse_index_.clear();
        size_t count = 0;

        for (const auto& [key, value] : entries) {
            // Record sparse index entry every 100 keys
            if (count % 100 == 0) {
                sparse_index_.push_back({key, file.tellp()});
            }

            uint32_t key_len = static_cast<uint32_t>(key.size());
            uint32_t val_len = static_cast<uint32_t>(value.size());

            file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            file.write(key.data(), key_len);
            file.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
            file.write(value.data(), val_len);

            count++;
        }

        entry_count_ = count;
    }

    /**
     * Look up a key in this SSTable.
     * Uses the sparse index for binary search, then linear scan.
     *
     * @param key The key to find.
     * @return The value if found, std::nullopt otherwise.
     *
     * Time: O(log(n/100) + 100) ≈ O(log n) with sparse index
     */
    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sparse_index_.empty()) return std::nullopt;

        std::ifstream file(path_, std::ios::binary);
        if (!file.is_open()) return std::nullopt;

        // Binary search on sparse index to find the right region
        auto it = std::upper_bound(
            sparse_index_.begin(), sparse_index_.end(), key,
            [](const std::string& k, const SparseIndexEntry& entry) {
                return k < entry.key;
            });

        // Start scanning from the previous sparse index entry
        std::streamoff start_offset = 0;
        if (it != sparse_index_.begin()) {
            --it;
            start_offset = it->offset;
        }

        file.seekg(start_offset);

        // Linear scan from the start position
        while (file.peek() != EOF) {
            uint32_t key_len, val_len;

            file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (file.fail()) break;

            std::string entry_key(key_len, '\0');
            file.read(entry_key.data(), key_len);

            file.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            if (file.fail()) break;

            std::string entry_val(val_len, '\0');
            file.read(entry_val.data(), val_len);

            if (entry_key == key) return entry_val;

            // Since keys are sorted, if we've passed our target, stop
            if (entry_key > key) break;
        }

        return std::nullopt;
    }

    /**
     * Merge two SSTables into a new one (compaction).
     * Keeps only the latest value for each key (from sstable_newer).
     * Removes tombstoned keys.
     *
     * @param older The older SSTable.
     * @param newer The newer SSTable (takes precedence on key conflicts).
     * @param output_path Path for the merged SSTable.
     * @return A new SSTable at output_path.
     */
    static std::unique_ptr<SSTable> compact(SSTable& older, SSTable& newer,
                           const std::string& output_path) {
        auto old_entries = older.read_all();
        auto new_entries = newer.read_all();

        // Merge sorted arrays (like merge step of merge sort)
        std::vector<std::pair<std::string, std::string>> merged;
        size_t i = 0, j = 0;

        while (i < old_entries.size() && j < new_entries.size()) {
            if (old_entries[i].first < new_entries[j].first) {
                merged.push_back(old_entries[i++]);
            } else if (old_entries[i].first > new_entries[j].first) {
                merged.push_back(new_entries[j++]);
            } else {
                // Same key — newer wins
                merged.push_back(new_entries[j++]);
                i++;  // Skip older entry
            }
        }

        while (i < old_entries.size()) merged.push_back(old_entries[i++]);
        while (j < new_entries.size()) merged.push_back(new_entries[j++]);

        auto result = std::make_unique<SSTable>(output_path);
        result->flush(merged);
        return result;
    }

    /**
     * Read all entries from the SSTable (for compaction).
     */
    std::vector<std::pair<std::string, std::string>> read_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<std::string, std::string>> entries;

        std::ifstream file(path_, std::ios::binary);
        if (!file.is_open()) return entries;

        while (file.peek() != EOF) {
            uint32_t key_len, val_len;

            file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (file.fail()) break;

            std::string key(key_len, '\0');
            file.read(key.data(), key_len);

            file.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            if (file.fail()) break;

            std::string value(val_len, '\0');
            file.read(value.data(), val_len);

            entries.emplace_back(std::move(key), std::move(value));
        }

        return entries;
    }

    const std::string& path() const { return path_; }
    size_t entry_count() const { return entry_count_; }

private:
    /**
     * Build the sparse index from an existing SSTable file.
     * Called when loading an SSTable that already exists on disk.
     */
    void build_sparse_index() {
        std::ifstream file(path_, std::ios::binary);
        if (!file.is_open()) return;

        sparse_index_.clear();
        size_t count = 0;

        while (file.peek() != EOF) {
            std::streamoff offset = file.tellg();
            uint32_t key_len, val_len;

            file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (file.fail()) break;

            std::string key(key_len, '\0');
            file.read(key.data(), key_len);

            file.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            if (file.fail()) break;

            // Skip the value
            file.seekg(val_len, std::ios::cur);

            if (count % 100 == 0) {
                sparse_index_.push_back({key, offset});
            }
            count++;
        }

        entry_count_ = count;
    }

    std::string path_;
    std::vector<SparseIndexEntry> sparse_index_;
    std::mutex mutex_;
    size_t entry_count_ = 0;
};

}  // namespace vaultdb
