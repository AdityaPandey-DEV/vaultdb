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
    explicit WAL(const std::string& path);
    ~WAL();

    // Non-copyable
    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    /**
     * Append a SET or DELETE operation to the WAL.
     * This MUST complete before the operation is applied to MemTable.
     *
     * @param op The operation type (SET or DELETE).
     * @param key The key being modified.
     * @param value The value (empty string for DELETE).
     */
    void append(WALOp op, const std::string& key, const std::string& value);

    /**
     * Recover all entries from the WAL file.
     * Called on server startup to rebuild MemTable state after a crash.
     *
     * @return Vector of WAL entries in chronological order.
     */
    std::vector<WALEntry> recover();

    /**
     * Checkpoint: truncate the WAL after a successful MemTable flush.
     * All entries have been persisted to SSTable, so the WAL is no longer needed.
     */
    void checkpoint();

    size_t entry_count() const { return entry_count_; }

private:
    std::string path_;
    std::ofstream file_;
    std::mutex mutex_;
    size_t entry_count_ = 0;
};

}  // namespace vaultdb
