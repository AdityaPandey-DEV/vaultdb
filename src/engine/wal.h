#pragma once
/**
 * VaultDB — Write-Ahead Log (WAL)
 *
 * WAL ensures atomicity — we write to WAL FIRST, then apply to MemTable.
 * If the server crashes mid-write, we can recover by replaying the WAL.
 *
 * Binary format of each entry:
 *   [timestamp: 8 bytes, uint64_t]
 *   [operation: 1 byte, uint8_t — 1=SET, 2=DELETE]
 *   [key_length: 4 bytes, uint32_t]
 *   [key: key_length bytes]
 *   [value_length: 4 bytes, uint32_t]
 *   [value: value_length bytes]
 *
 * The WAL is append-only. It is truncated (checkpointed) after a
 * successful MemTable flush to SSTable.
 */

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>

namespace vaultdb {

enum class WALOp : uint8_t {
    SET = 1,
    DELETE = 2,
};

struct WALEntry {
    uint64_t timestamp;
    WALOp op;
    std::string key;
    std::string value;
};

class WAL {
public:
    /**
     * Initialize the WAL at the given file path.
     * Creates the file and parent directories if they don't exist.
     */
    explicit WAL(const std::string& path) : path_(path) {
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());

        // Open in append + binary mode
        file_.open(path_, std::ios::binary | std::ios::app);
    }

    ~WAL() {
        if (file_.is_open()) file_.close();
    }

    /**
     * Append a SET or DELETE operation to the WAL.
     * This MUST complete before the operation is applied to MemTable.
     *
     * @param op The operation type (SET or DELETE).
     * @param key The key being modified.
     * @param value The value (empty string for DELETE).
     */
    void append(WALOp op, const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        uint8_t op_byte = static_cast<uint8_t>(op);
        uint32_t key_len = static_cast<uint32_t>(key.size());
        uint32_t val_len = static_cast<uint32_t>(value.size());

        // Write binary: timestamp | op | key_len | key | val_len | val
        file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
        file_.write(reinterpret_cast<const char*>(&op_byte), sizeof(op_byte));
        file_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        file_.write(key.data(), key_len);
        file_.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        file_.write(value.data(), val_len);

        // Flush to disk immediately — critical for durability
        file_.flush();
        entry_count_++;
    }

    /**
     * Recover all entries from the WAL file.
     * Called on server startup to rebuild MemTable state after a crash.
     *
     * @return Vector of WAL entries in chronological order.
     */
    std::vector<WALEntry> recover() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<WALEntry> entries;

        std::ifstream reader(path_, std::ios::binary);
        if (!reader.is_open()) return entries;

        while (reader.peek() != EOF) {
            WALEntry entry;
            uint8_t op_byte;
            uint32_t key_len, val_len;

            reader.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
            reader.read(reinterpret_cast<char*>(&op_byte), sizeof(op_byte));
            reader.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));

            if (reader.fail()) break;  // Incomplete entry (crash during write)

            entry.op = static_cast<WALOp>(op_byte);
            entry.key.resize(key_len);
            reader.read(entry.key.data(), key_len);

            reader.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            if (reader.fail()) break;

            entry.value.resize(val_len);
            reader.read(entry.value.data(), val_len);

            if (reader.fail()) break;

            entries.push_back(std::move(entry));
        }

        return entries;
    }

    /**
     * Checkpoint: truncate the WAL after a successful MemTable flush.
     * All entries have been persisted to SSTable, so the WAL is no longer needed.
     */
    void checkpoint() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Close current file
        if (file_.is_open()) file_.close();

        // Truncate by opening in truncate + out mode
        file_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (file_.is_open()) file_.close();

        // Reopen in append mode
        file_.open(path_, std::ios::binary | std::ios::app);
        entry_count_ = 0;
    }

    size_t entry_count() const { return entry_count_; }

private:
    std::string path_;
    std::ofstream file_;
    std::mutex mutex_;
    size_t entry_count_ = 0;
};

}  // namespace vaultdb
