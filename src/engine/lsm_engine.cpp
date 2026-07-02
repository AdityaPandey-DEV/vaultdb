/**
 * VaultDB — LSM Engine Implementation
 *
 * This is the coordinator that wires together all storage components.
 * Key implementation details:
 *
 *   Write path: WAL → MemTable → (flush if full) → SSTable
 *   Read path:  Cache → MemTable → SSTables (newest first)
 *
 * Thread safety: engine_mutex_ protects all public operations. Each
 * component also has its own mutex for internal thread safety, but the
 * engine mutex provides the top-level serialization guarantee.
 *
 * TTL handling: Values with TTL are stored as "EXPIRY:timestamp:value".
 * On GET, we check if the timestamp has passed. If expired, we lazily
 * tombstone the key and return nullopt — no separate expiry thread needed.
 */

#include "lsm_engine.h"

namespace vaultdb {

// Prefix used to encode TTL expiry timestamps in stored values
static const std::string EXPIRY_PREFIX = "EXPIRY:";

LSMEngine::LSMEngine(const std::string& data_dir,
                     size_t cache_size,
                     size_t memtable_size_bytes,
                     int compaction_interval_sec)
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
    // Uses a polling loop with short sleep intervals so the thread
    // can respond quickly to shutdown signals (running_ = false)
    compaction_thread_ = std::thread([this, compaction_interval_sec]() {
        int elapsed_ms = 0;
        const int check_interval_ms = 100;
        const int target_interval_ms = compaction_interval_sec * 1000;

        while (running_) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(check_interval_ms));
            elapsed_ms += check_interval_ms;
            if (elapsed_ms >= target_interval_ms) {
                elapsed_ms = 0;
                if (running_) compact();
            }
        }
    });
}

LSMEngine::~LSMEngine() {
    running_ = false;
    if (compaction_thread_.joinable()) {
        compaction_thread_.join();
    }
}

void LSMEngine::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(engine_mutex_);

    // Step 1: WAL first (ensures durability — if we crash after this,
    //         the entry can be recovered on restart)
    wal_.append(WALOp::SET, key, value);

    // Step 2: MemTable (in-memory sorted buffer)
    memtable_.set(key, value);

    // Step 3: Update cache (so subsequent reads are fast)
    cache_.put(key, value);

    // Step 4: Flush if MemTable is full
    if (memtable_.is_full()) {
        flush_memtable();
    }

    write_count_++;
}

std::optional<std::string> LSMEngine::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    read_count_++;

    // Step 1: Check LRU cache — O(1), fastest path
    auto cached = cache_.get(key);
    if (cached.has_value()) {
        cache_hits_++;
        if (cached.value() == TOMBSTONE) return std::nullopt;
        return check_ttl(key, cached.value());
    }

    // Step 2: Check MemTable — O(log n), still in-memory
    auto mem_result = memtable_.get(key);
    if (mem_result.has_value()) {
        if (mem_result.value() == TOMBSTONE) return std::nullopt;
        cache_.put(key, mem_result.value());
        return check_ttl(key, mem_result.value());
    }

    // Step 3: Check SSTables newest to oldest — O(log n) per table
    // Newest first because newer values supersede older ones
    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        // Bloom Filter pre-check: skip this SSTable entirely if the
        // filter says the key is definitely not present
        if (!(*it)->bloom_might_contain(key)) {
            bloom_saved_++;
            continue;
        }

        auto disk_result = (*it)->get(key);
        if (disk_result.has_value()) {
            if (disk_result.value() == TOMBSTONE) return std::nullopt;
            cache_.put(key, disk_result.value());
            return check_ttl(key, disk_result.value());
        }
    }

    return std::nullopt;  // Key not found anywhere
}

void LSMEngine::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(engine_mutex_);

    // Write tombstone to WAL and MemTable
    // The tombstone propagates through flush → SSTable → compaction
    wal_.append(WALOp::DELETE, key, "");
    memtable_.del(key);
    cache_.remove(key);

    if (memtable_.is_full()) {
        flush_memtable();
    }
}

LSMEngine::Stats LSMEngine::get_stats() {
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
        bloom_saved_,
    };
}

/**
 * Check if a value has TTL and whether it has expired.
 *
 * TTL values are stored as: "EXPIRY:<timestamp_ms>:<actual_value>"
 * If the current time exceeds the timestamp, the key is expired.
 *
 * Lazy expiry strategy: we tombstone expired keys on read rather than
 * running a background expiry thread. This is simpler and avoids the
 * overhead of scanning all keys periodically.
 */
std::optional<std::string> LSMEngine::check_ttl(const std::string& key,
                                                 const std::string& value) {
    // Fast path: most values don't have TTL
    if (value.size() < EXPIRY_PREFIX.size() ||
        value.compare(0, EXPIRY_PREFIX.size(), EXPIRY_PREFIX) != 0) {
        return value;  // No TTL — return as-is
    }

    // Parse: "EXPIRY:<timestamp>:<actual_value>"
    size_t first_colon = EXPIRY_PREFIX.size() - 1;
    size_t second_colon = value.find(':', first_colon + 1);
    if (second_colon == std::string::npos) {
        return value;  // Malformed — return as-is rather than losing data
    }

    int64_t expiry_ms;
    try {
        expiry_ms = std::stoll(
            value.substr(first_colon + 1, second_colon - first_colon - 1));
    } catch (...) {
        return value;  // Malformed — return as-is
    }

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (now_ms >= expiry_ms) {
        // Expired — lazily tombstone the key so future reads don't find it
        // Note: we don't write to WAL here because we're inside a read.
        // The tombstone in MemTable is sufficient until next flush/restart.
        memtable_.del(key);
        cache_.remove(key);
        return std::nullopt;
    }

    // Not expired — strip the EXPIRY prefix and return the actual value
    return value.substr(second_colon + 1);
}

void LSMEngine::flush_memtable() {
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

    // Clear MemTable and checkpoint WAL (entries are now safe in SSTable)
    memtable_.clear();
    wal_.checkpoint();
}

void LSMEngine::recover() {
    auto entries = wal_.recover();
    for (const auto& entry : entries) {
        if (entry.op == WALOp::SET) {
            memtable_.set(entry.key, entry.value);
        } else if (entry.op == WALOp::DELETE) {
            memtable_.del(entry.key);
        }
    }

    if (!entries.empty()) {
        // If MemTable is full after recovery, flush immediately
        if (memtable_.is_full()) {
            flush_memtable();
        }
    }
}

void LSMEngine::load_existing_sstables() {
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

void LSMEngine::compact() {
    std::lock_guard<std::mutex> lock(engine_mutex_);

    if (sstables_.size() <= 4) return;

    // Merge the two oldest SSTables (first two in the vector)
    auto& older = sstables_[0];
    auto& newer = sstables_[1];

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::string output_path = data_dir_ + "/sstables/sstable_" +
                              std::to_string(ms) + "_compacted.sst";

    auto merged = SSTable::compact(*older, *newer, output_path);

    // Delete old SSTable files from disk
    std::filesystem::remove(older->path());
    std::filesystem::remove(newer->path());

    // Replace the two oldest with the merged one
    sstables_.erase(sstables_.begin(), sstables_.begin() + 2);
    sstables_.insert(sstables_.begin(), std::move(merged));
}

}  // namespace vaultdb
