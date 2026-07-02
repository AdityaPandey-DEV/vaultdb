#pragma once
/**
 * VaultDB — LSM Engine (Log-Structured Merge Tree)
 *
 * Coordinates all storage components:
 *   MemTable (in-memory) → SSTable (on-disk) → LRU Cache (hot keys)
 *
 * Write path:
 *   1. Write to WAL first (durability)
 *   2. Write to MemTable
 *   3. If MemTable full → flush to SSTable → clear MemTable → WAL checkpoint
 *
 * Read path:
 *   1. Check LRU cache (fastest — O(1))
 *   2. Check MemTable (in-memory — O(log n))
 *   3. Check SSTables newest to oldest (disk — O(log n) per table)
 *   4. Store result in LRU cache for future reads
 *
 * TTL support:
 *   Values set with TTL are stored as "EXPIRY:<timestamp_ms>:<value>".
 *   On GET, the engine checks if the timestamp has passed. If expired,
 *   the key is lazily tombstoned and nullopt is returned.
 *
 * Background compaction: merges SSTables when count > 4 to reduce
 * read amplification.
 */

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../cache/lru_cache.h"
#include "memtable.h"
#include "sstable.h"
#include "wal.h"

namespace vaultdb {

class LSMEngine {
public:
    /**
     * Initialize the LSM engine.
     * @param data_dir Directory for data files (WAL, SSTables).
     * @param cache_size Number of entries in the LRU cache.
     * @param memtable_size_bytes MemTable flush threshold.
     * @param compaction_interval_sec Seconds between compaction checks.
     */
    LSMEngine(const std::string& data_dir = "./data",
              size_t cache_size = 10000,
              size_t memtable_size_bytes = 4 * 1024 * 1024,
              int compaction_interval_sec = 60);

    ~LSMEngine();

    // Non-copyable, non-movable
    LSMEngine(const LSMEngine&) = delete;
    LSMEngine& operator=(const LSMEngine&) = delete;

    /**
     * SET: Store a key-value pair.
     * Write path: WAL → MemTable → (flush if full) → SSTable
     */
    void set(const std::string& key, const std::string& value);

    /**
     * GET: Retrieve the value for a key.
     * Read path: Cache → MemTable → SSTables (newest first)
     */
    std::optional<std::string> get(const std::string& key);

    /**
     * DEL: Delete a key by writing a tombstone.
     */
    void del(const std::string& key);

    /**
     * Get engine statistics for the dashboard.
     */
    struct Stats {
        size_t write_count;
        size_t read_count;
        size_t cache_hits;
        size_t cache_size;
        size_t memtable_size_bytes;
        size_t memtable_entries;
        size_t sstable_count;
        double cache_hit_rate;
    };

    Stats get_stats();

private:
    /** Flush the current MemTable to a new SSTable on disk. */
    void flush_memtable();

    /** Recover from WAL after a crash. */
    void recover();

    /** Load existing SSTable files from disk on startup. */
    void load_existing_sstables();

    /** Background compaction: merge SSTables when count exceeds 4. */
    void compact();

    /**
     * Check if a value has a TTL prefix and whether it has expired.
     * Returns the actual value (stripped of prefix) if not expired,
     * or std::nullopt if expired (also tombstones the key lazily).
     */
    std::optional<std::string> check_ttl(const std::string& key,
                                          const std::string& value);

    // Components
    std::string data_dir_;
    LRUCache<std::string, std::string> cache_;
    MemTable memtable_;
    WAL wal_;
    std::vector<std::unique_ptr<SSTable>> sstables_;

    // Threading
    std::mutex engine_mutex_;
    std::thread compaction_thread_;
    std::atomic<bool> running_;

    // Stats
    std::atomic<size_t> write_count_{0};
    std::atomic<size_t> read_count_{0};
    std::atomic<size_t> cache_hits_{0};
};

}  // namespace vaultdb
