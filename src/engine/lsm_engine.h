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
              int compaction_interval_sec = 60)
        : data_dir_(data_dir),
          cache_(cache_size),
          memtable_(memtable_size_bytes),
          wal_(data_dir + "/wal.log"),
          running_(true) {

        std::filesystem::create_directories(data_dir_);
        std::filesystem::create_directories(data_dir_ + "/sstables");

        // Load existing SSTables from disk
        load_existing_sstables();

        // Recover from WAL (replay any entries from before a crash)
        recover();

        // Start background compaction thread
        compaction_thread_ = std::thread([this, compaction_interval_sec]() {
            int elapsed_ms = 0;
            const int check_interval_ms = 100;
            const int target_interval_ms = compaction_interval_sec * 1000;
            
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
                elapsed_ms += check_interval_ms;
                if (elapsed_ms >= target_interval_ms) {
                    elapsed_ms = 0;
                    if (running_) compact();
                }
            }
        });
    }

    ~LSMEngine() {
        running_ = false;
        if (compaction_thread_.joinable()) {
            compaction_thread_.join();
        }
    }

    // Non-copyable, non-movable
    LSMEngine(const LSMEngine&) = delete;
    LSMEngine& operator=(const LSMEngine&) = delete;

    /**
     * SET: Store a key-value pair.
     *
     * 1. Write to WAL first (ensures durability)
     * 2. Write to MemTable
     * 3. Update LRU cache
     * 4. If MemTable is full, flush to SSTable
     */
    void set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(engine_mutex_);

        // Step 1: WAL first
        wal_.append(WALOp::SET, key, value);

        // Step 2: MemTable
        memtable_.set(key, value);

        // Step 3: Update cache
        cache_.put(key, value);

        // Step 4: Flush if MemTable is full
        if (memtable_.is_full()) {
            flush_memtable();
        }

        write_count_++;
    }

    /**
     * GET: Retrieve the value for a key.
     *
     * Search order (fastest to slowest):
     * 1. LRU Cache (O(1))
     * 2. MemTable (O(log n))
     * 3. SSTables, newest first (O(log n) per table)
     *
     * If found in SSTable, result is added to LRU cache.
     */
    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        read_count_++;

        // Step 1: Check LRU cache
        auto cached = cache_.get(key);
        if (cached.has_value()) {
            cache_hits_++;
            if (cached.value() == TOMBSTONE) return std::nullopt;
            return cached;
        }

        // Step 2: Check MemTable
        auto mem_result = memtable_.get(key);
        if (mem_result.has_value()) {
            if (mem_result.value() == TOMBSTONE) return std::nullopt;
            cache_.put(key, mem_result.value());
            return mem_result;
        }

        // Step 3: Check SSTables (newest first)
        for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
            auto disk_result = (*it)->get(key);
            if (disk_result.has_value()) {
                if (disk_result.value() == TOMBSTONE) return std::nullopt;
                cache_.put(key, disk_result.value());
                return disk_result;
            }
        }

        return std::nullopt;  // Key not found anywhere
    }

    /**
     * DEL: Delete a key by writing a tombstone.
     * The tombstone propagates through MemTable → SSTable during flush.
     * During compaction, tombstoned keys are removed.
     */
    void del(const std::string& key) {
        std::lock_guard<std::mutex> lock(engine_mutex_);

        wal_.append(WALOp::DELETE, key, "");
        memtable_.del(key);
        cache_.remove(key);

        if (memtable_.is_full()) {
            flush_memtable();
        }
    }

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

    Stats get_stats() {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        return Stats{
            write_count_,
            read_count_,
            cache_hits_,
            cache_.size(),
            memtable_.size_bytes(),
            memtable_.count(),
            sstables_.size(),
            read_count_ > 0
                ? static_cast<double>(cache_hits_) / read_count_ * 100.0
                : 0.0,
        };
    }

private:
    /**
     * Flush the current MemTable to a new SSTable on disk.
     * Called when MemTable exceeds its size threshold.
     */
    void flush_memtable() {
        auto entries = memtable_.get_sorted_entries();
        if (entries.empty()) return;

        // Generate unique SSTable filename using timestamp
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::string path = data_dir_ + "/sstables/sstable_" +
                           std::to_string(ms) + ".sst";

        auto sstable = std::make_unique<SSTable>(path);
        sstable->flush(entries);
        sstables_.push_back(std::move(sstable));

        memtable_.clear();
        wal_.checkpoint();
    }

    /**
     * Recover from WAL after a crash.
     * Replays all WAL entries into the MemTable.
     */
    void recover() {
        auto entries = wal_.recover();
        for (const auto& entry : entries) {
            if (entry.op == WALOp::SET) {
                memtable_.set(entry.key, entry.value);
            } else if (entry.op == WALOp::DELETE) {
                memtable_.del(entry.key);
            }
        }

        if (!entries.empty()) {
            // If MemTable is full after recovery, flush
            if (memtable_.is_full()) {
                flush_memtable();
            }
        }
    }

    /**
     * Load existing SSTable files from disk on startup.
     * Files are sorted by name (which contains timestamp) for correct ordering.
     */
    void load_existing_sstables() {
        std::string sstable_dir = data_dir_ + "/sstables";
        if (!std::filesystem::exists(sstable_dir)) return;

        std::vector<std::string> paths;
        for (const auto& entry :
             std::filesystem::directory_iterator(sstable_dir)) {
            if (entry.path().extension() == ".sst") {
                paths.push_back(entry.path().string());
            }
        }

        // Sort by filename (timestamp-based) so newest is last
        std::sort(paths.begin(), paths.end());

        for (const auto& path : paths) {
            sstables_.push_back(std::make_unique<SSTable>(path));
        }
    }

    /**
     * Background compaction: merge SSTables when count exceeds 4.
     * Merges the two oldest SSTables into one.
     */
    void compact() {
        std::lock_guard<std::mutex> lock(engine_mutex_);

        if (sstables_.size() <= 4) return;

        // Merge the two oldest SSTables
        auto& older = sstables_[0];
        auto& newer = sstables_[1];

        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::string output_path = data_dir_ + "/sstables/sstable_" +
                                  std::to_string(ms) + "_compacted.sst";

        auto merged = SSTable::compact(*older, *newer, output_path);

        // Delete old SSTable files
        std::filesystem::remove(older->path());
        std::filesystem::remove(newer->path());

        // Replace the two oldest with the merged one
        sstables_.erase(sstables_.begin(), sstables_.begin() + 2);
        sstables_.insert(sstables_.begin(), std::move(merged));
    }

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
