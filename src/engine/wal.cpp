/**
 * VaultDB — Write-Ahead Log Implementation
 *
 * All WAL operations are protected by a mutex so multiple threads
 * can safely append entries concurrently.
 */

#include "wal.h"

namespace vaultdb {

WAL::WAL(const std::string& path) : path_(path) {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());

    // Open in append + binary mode — new entries go to end of file
    file_.open(path_, std::ios::binary | std::ios::app);
}

WAL::~WAL() {
    if (file_.is_open()) file_.close();
}

void WAL::append(WALOp op, const std::string& key, const std::string& value) {
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

std::vector<WALEntry> WAL::recover() {
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

void WAL::checkpoint() {
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

}  // namespace vaultdb
