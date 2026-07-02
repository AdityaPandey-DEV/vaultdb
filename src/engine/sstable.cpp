/**
 * VaultDB — SSTable Implementation
 *
 * Sorted String Table: immutable on-disk file of sorted key-value pairs.
 *
 * Key design decisions:
 *   - Sparse index every 100 keys: keeps memory low while still giving
 *     O(log(n/100) + 100) ≈ O(log n) lookups
 *   - Compaction uses merge-sort: both SSTables are already sorted, so
 *     merging is O(n + m) where n, m are the entry counts
 */

#include "sstable.h"

namespace vaultdb {

SSTable::SSTable(const std::string& path) : path_(path) {
    if (std::filesystem::exists(path)) {
        build_sparse_index();
    }
}

void SSTable::flush(const std::vector<std::pair<std::string, std::string>>& entries) {
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

std::optional<std::string> SSTable::get(const std::string& key) {
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

std::unique_ptr<SSTable> SSTable::compact(SSTable& older, SSTable& newer,
                                          const std::string& output_path) {
    auto old_entries = older.read_all();
    auto new_entries = newer.read_all();

    // Merge sorted arrays (like merge step of merge sort)
    // This is O(n + m) because both inputs are already sorted
    std::vector<std::pair<std::string, std::string>> merged;
    size_t i = 0, j = 0;

    while (i < old_entries.size() && j < new_entries.size()) {
        if (old_entries[i].first < new_entries[j].first) {
            merged.push_back(old_entries[i++]);
        } else if (old_entries[i].first > new_entries[j].first) {
            merged.push_back(new_entries[j++]);
        } else {
            // Same key — newer wins (it has the latest value)
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

std::vector<std::pair<std::string, std::string>> SSTable::read_all() {
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

void SSTable::build_sparse_index() {
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

        // Skip the value data — we only need keys for the index
        file.seekg(val_len, std::ios::cur);

        if (count % 100 == 0) {
            sparse_index_.push_back({key, offset});
        }
        count++;
    }

    entry_count_ = count;
}

}  // namespace vaultdb
