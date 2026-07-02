# 4. File I/O & Binary Data

VaultDB stores data on disk in binary format for maximum efficiency. This guide explains how binary file I/O works in C++.

---

## 4.1 Text Files vs Binary Files

### Text file (human-readable):
```
SET key1 value1
SET key2 value2
```
**Problem:** How do you know where `key1` ends and `value1` begins if the value contains spaces? What if the value contains newline characters? Text parsing is fragile and slow.

### Binary file (machine-readable):
```
[4 bytes: key_length=4][4 bytes: "key1"][6 bytes: val_length=6][6 bytes: "value1"]
```
**Advantage:** We know EXACTLY how many bytes to read. No parsing ambiguity. Faster to read/write.

---

## 4.2 `std::ofstream` and `std::ifstream`

```cpp
// Writing to a file (o = output)
std::ofstream file;
file.open("data.bin", std::ios::binary | std::ios::app);

// Reading from a file (i = input)
std::ifstream file;
file.open("data.bin", std::ios::binary);
```

### File open flags:

| Flag | Meaning | When Used |
|------|---------|-----------|
| `std::ios::binary` | Don't translate `\n` characters | Always for binary files |
| `std::ios::app` | Append to end of file | WAL (always adds to the end) |
| `std::ios::trunc` | Erase file contents first | WAL checkpoint (clear old data) |
| `std::ios::in` | Open for reading | SSTable lookup |
| `std::ios::out` | Open for writing | SSTable flush |

### Combining flags with `|`:
```cpp
std::ios::binary | std::ios::app
// Both binary mode AND append mode
```

---

## 4.3 Writing Binary Data

### Writing a string (text):
```cpp
file.write(key.data(), key.size());
// key.data() → pointer to the string's characters
// key.size() → number of bytes to write
```

### Writing a number as raw bytes:
```cpp
uint32_t key_len = key.size();
file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
```

**Step by step:**
1. `key_len = 5` → in memory: `[05][00][00][00]` (4 bytes, little-endian)
2. `&key_len` → pointer to those 4 bytes
3. `reinterpret_cast<const char*>` → treat as a char pointer (file.write needs this)
4. `sizeof(key_len)` → 4 (write exactly 4 bytes)

**Result in file:** `05 00 00 00` (the raw bytes of the number 5)

---

## 4.4 Reading Binary Data

```cpp
// Reading a uint32_t from a binary file:
uint32_t key_len;
file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
// Now key_len == 5

// Reading a string of known length:
std::string key(key_len, '\0');  // Pre-allocate string of size key_len
file.read(&key[0], key_len);     // Read key_len bytes into the string
// Now key == "hello" (or whatever 5 characters were in the file)
```

---

## 4.5 VaultDB's WAL Binary Format

Each WAL entry is stored as:
```
[timestamp: 8 bytes] [operation: 1 byte] [key_len: 4 bytes] [key] [val_len: 4 bytes] [value]
```

**Writing a WAL entry:**
```cpp
void WAL::append(WALOp op, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);  // Thread-safe

    // 1. Write timestamp (8 bytes)
    uint64_t timestamp = std::chrono::system_clock::now()
                            .time_since_epoch().count();
    file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));

    // 2. Write operation type (1 byte: SET=1, DELETE=2)
    uint8_t op_byte = static_cast<uint8_t>(op);
    file_.write(reinterpret_cast<const char*>(&op_byte), sizeof(op_byte));

    // 3. Write key length + key
    uint32_t key_len = key.size();
    file_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    file_.write(key.data(), key_len);

    // 4. Write value length + value
    uint32_t val_len = value.size();
    file_.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
    file_.write(value.data(), val_len);

    file_.flush();  // Force write to disk immediately
    entry_count_++;
}
```

**Why binary instead of text?**
- Fixed-size numbers mean we always know exactly how many bytes to read
- No need for delimiters (like `\n` or spaces)
- 3-5x faster to parse than text

---

## 4.6 `std::filesystem` (C++17)

VaultDB uses C++17's filesystem library for directory operations:

```cpp
#include <filesystem>
namespace fs = std::filesystem;

// Create directories (like mkdir -p)
fs::create_directories("./data/sstables");

// Check if file exists
if (fs::exists("data/wal.bin")) { ... }

// List all files in a directory
for (auto& entry : fs::directory_iterator("./data")) {
    std::string path = entry.path().string();
    // path = "./data/0001.sst", "./data/0002.sst", etc.
}

// Get parent directory from a path
fs::path("./data/wal.bin").parent_path()  // Returns "./data"

// Remove a file
fs::remove("./data/old.sst");
```

**Before C++17**, you had to use POSIX functions (`opendir`, `readdir`, `stat`) which were platform-specific and much more code.

---

## 4.7 `std::streamoff` and File Positions

```cpp
// Get current position in file
std::streamoff position = file.tellg();  // tellg = "tell get position"

// Jump to a specific byte position
file.seekg(1024);  // Jump to byte 1024

// Jump relative to current position
file.seekg(100, std::ios::cur);  // Forward 100 bytes from current
```

**VaultDB uses this for the Sparse Index:**
```cpp
struct SparseIndexEntry {
    std::string key;
    std::streamoff offset;  // Byte position in the SSTable file
};
```

When looking up a key:
1. Binary search the sparse index to find the nearest key
2. `seekg(offset)` to jump to that position in the file
3. Linear scan forward to find the exact key

---

## Summary

| Concept | What It Does | VaultDB Usage |
|---------|-------------|---------------|
| `std::ofstream` | Write to files | WAL append, SSTable flush |
| `std::ifstream` | Read from files | WAL recovery, SSTable lookup |
| `reinterpret_cast` | Type-pun bytes for binary I/O | Write numbers as raw bytes |
| `file.flush()` | Force data to disk | WAL durability |
| `std::filesystem` | Portable file/dir operations | Create dirs, list SSTables |
| `seekg` / `tellg` | Jump to file positions | Sparse index lookups |
