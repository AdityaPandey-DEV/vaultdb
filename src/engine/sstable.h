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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <filesystem>

#include "bloom_filter.h"

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
    explicit SSTable(const std::string& path);

    /**
     * Flush a sorted vector of key-value pairs to disk as a new SSTable.
     * The entries MUST be sorted by key (guaranteed by MemTable's std::map).
     *
     * @param entries Sorted vector of (key, value) pairs from MemTable.
     */
    void flush(const std::vector<std::pair<std::string, std::string>>& entries);

    /**
     * Look up a key in this SSTable.
     * Uses the sparse index for binary search, then linear scan.
     *
     * @param key The key to find.
     * @return The value if found, std::nullopt otherwise.
     *
     * Time: O(log(n/100) + 100) ≈ O(log n) with sparse index
     */
    std::optional<std::string> get(const std::string& key);

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
                                            const std::string& output_path);

    /**
     * Read all entries from the SSTable (for compaction).
     */
    std::vector<std::pair<std::string, std::string>> read_all();

    const std::string& path() const { return path_; }
    size_t entry_count() const { return entry_count_; }

    /**
     * Check the Bloom Filter for a key without touching disk.
     * @return false = key is definitely NOT here (skip disk I/O)
     *         true  = key might be here (proceed with disk lookup)
     */
    bool bloom_might_contain(const std::string& key) const;

private:
    /**
     * Build the sparse index from an existing SSTable file.
     * Called when loading an SSTable that already exists on disk.
     */
    void build_sparse_index();

    std::string path_;
    std::vector<SparseIndexEntry> sparse_index_;
    BloomFilter bloom_filter_;
    std::mutex mutex_;
    size_t entry_count_ = 0;
};

}  // namespace vaultdb
