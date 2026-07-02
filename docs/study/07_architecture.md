# 7. Architecture Walkthrough — Full Request Flow

This document traces a complete request through VaultDB, from the moment a client connects to the moment data hits disk.

---

## 7.1 System Overview

```
┌─────────────┐      TCP/6379     ┌─────────────────────────────────────┐
│   Client    │ ──────────────── │           VaultDB Server             │
│  (nc, Python│                   │                                     │
│   benchmark)│                   │  ┌─────────┐   ┌──────────────────┐│
│             │                   │  │ Server  │──▶│     Parser       ││
│SET key val\n│──────────────────▶│  │(select) │   │(parse + execute) ││
│             │                   │  └─────────┘   └────────┬─────────┘│
│             │                   │                          │          │
│             │                   │                 ┌────────▼────────┐ │
│             │                   │                 │   LSM Engine    │ │
│             │                   │                 │                 │ │
│             │                   │                 │  ┌───┐ ┌──────┐│ │
│         OK\n│◀──────────────────│                 │  │WAL│ │Cache ││ │
│             │                   │                 │  └───┘ └──────┘│ │
│             │                   │                 │  ┌────────────┐│ │
│             │                   │                 │  │  MemTable  ││ │
│             │                   │                 │  └─────┬──────┘│ │
│             │                   │                 │        │flush  │ │
│             │                   │                 │  ┌─────▼──────┐│ │
│             │                   │                 │  │  SSTables  ││ │
│             │                   │                 │  └────────────┘│ │
│             │                   │                 └────────────────┘ │
└─────────────┘                   └─────────────────────────────────────┘
```

---

## 7.2 Walkthrough: `SET user Aditya`

### Step 1: Client sends the command
```
Terminal: nc localhost 6379
User types: SET user Aditya
```
The `nc` (netcat) tool sends the bytes `SET user Aditya\n` over a TCP connection to port 6379.

### Step 2: Server receives bytes
```cpp
// server.cpp — inside the select() loop
bool Server::handle_client(int fd) {
    char buffer[4096];
    ssize_t bytes_read = recv(fd, buffer, sizeof(buffer), 0);
    // bytes_read = 16, buffer = "SET user Aditya\n"

    connections_[fd].append(buffer, bytes_read);
    // Connection buffer now has: "SET user Aditya\n"

    while (auto cmd_str = connections_[fd].extract_command()) {
        // extract_command() finds \n and returns "SET user Aditya"
        Command cmd = Parser::parse(cmd_str.value());
        std::string response = Parser::execute(cmd, engine_);
        send(fd, response.c_str(), response.size(), 0);
    }
}
```

### Step 3: Parser splits the command
```cpp
// parser.cpp
Command Parser::parse(const std::string& input) {
    // input = "SET user Aditya"
    // Tokenize by spaces:
    //   tokens = ["SET", "user", "Aditya"]

    Command cmd;
    cmd.name = "SET";           // tokens[0]
    cmd.args = {"user", "Aditya"};  // tokens[1], tokens[2]
    return cmd;
}
```

### Step 4: Parser executes against the engine
```cpp
// parser.cpp
std::string Parser::execute(const Command& cmd, LSMEngine& engine) {
    if (cmd.name == "SET") {
        engine.set(cmd.args[0], cmd.args[1]);
        // engine.set("user", "Aditya")
        return "OK\n";
    }
}
```

### Step 5: LSM Engine processes the write
```cpp
// lsm_engine.cpp
void LSMEngine::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    // ^^^^ Lock the entire engine — only one thread can write at a time

    // 5a. Write to WAL first (durability)
    wal_.append(WALOp::SET, key, value);
    // Appends binary data to data/wal.bin

    // 5b. Write to MemTable (speed)
    memtable_.set(key, value);
    // Inserts into sorted std::map

    // 5c. Update cache (for future reads)
    cache_.put(key, value);
    // Puts at head of LRU list

    // 5d. Check if MemTable is full
    if (memtable_.is_full()) {
        flush_memtable();
    }

    write_count_++;  // Atomic increment for stats
}
```

### Step 6: If MemTable is full → Flush to SSTable
```cpp
void LSMEngine::flush_memtable() {
    // Get all entries sorted by key
    auto entries = memtable_.get_sorted_entries();
    // entries = [("apple","1"), ("banana","2"), ("user","Aditya"), ...]

    // Create new SSTable file
    std::string path = data_dir_ + "/" + generate_filename();
    auto sstable = std::make_unique<SSTable>(path);

    // Write all entries to disk in sorted order
    sstable->flush(entries);

    // Add to our list of SSTables
    sstables_.push_back(std::move(sstable));

    // Clear MemTable (it's now on disk)
    memtable_.clear();

    // Checkpoint WAL (truncate — entries are safe in SSTable now)
    wal_.checkpoint();
}
```

### Step 7: Response sent back to client
```
Server sends: "OK\n"
Client sees: OK
```

---

## 7.3 Walkthrough: `GET user`

### Step 1-3: Same as SET (receive, parse)
Parser returns `cmd.name = "GET"`, `cmd.args = {"user"}`

### Step 4: Engine read path
```cpp
std::optional<std::string> LSMEngine::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    read_count_++;

    // 4a. Check LRU Cache first (fastest — O(1))
    auto cached = cache_.get(key);
    if (cached.has_value()) {
        cache_hits_++;
        return check_ttl(key, cached.value());
        // check_ttl verifies key hasn't expired
    }

    // 4b. Check MemTable (in-memory — O(log n))
    auto mem_val = memtable_.get(key);
    if (mem_val.has_value()) {
        if (mem_val.value() == TOMBSTONE) return std::nullopt;  // Deleted!
        cache_.put(key, mem_val.value());  // Cache for future
        return check_ttl(key, mem_val.value());
    }

    // 4c. Check SSTables (newest to oldest — O(log n) per table)
    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        //       ^^^^^^^^ reverse iterator — newest first!
        auto val = (*it)->get(key);
        if (val.has_value()) {
            if (val.value() == TOMBSTONE) return std::nullopt;
            cache_.put(key, val.value());
            return check_ttl(key, val.value());
        }
    }

    // 4d. Key not found anywhere
    return std::nullopt;
}
```

### Step 5: Response
```cpp
if (result.has_value()) {
    return "VALUE " + result.value() + "\n";  // "VALUE Aditya\n"
} else {
    return "NULL\n";
}
```

---

## 7.4 Walkthrough: Crash Recovery

### The crash:
```
1. Server receives SET key1 val1 → WAL written ✓, MemTable written ✓
2. Server receives SET key2 val2 → WAL written ✓, MemTable written ✓
3. CRASH! (kill -9, power outage, etc.)
   MemTable is GONE (it was in RAM)
   WAL is SAFE (it was on disk)
```

### The recovery (on restart):
```cpp
void LSMEngine::recover() {
    // Read all WAL entries from disk
    auto entries = wal_.recover();

    // Replay them into the new MemTable
    for (auto& entry : entries) {
        if (entry.op == WALOp::SET) {
            memtable_.set(entry.key, entry.value);
        } else if (entry.op == WALOp::DELETE) {
            memtable_.del(entry.key);
        }
    }
    // MemTable now has: {"key1": "val1", "key2": "val2"}
    // Data recovered! No loss!
}
```

---

## 7.5 Walkthrough: TTL (Time-To-Live)

### Setting a key with TTL:
```
TTL session 30 abc123
```

```cpp
// Parser converts TTL to a SET with encoded expiry:
auto now = std::chrono::system_clock::now();
auto expiry = now + std::chrono::seconds(30);
auto expiry_ms = expiry.time_since_epoch().count();
std::string encoded = "EXPIRY:" + std::to_string(expiry_ms) + ":" + "abc123";
engine.set("session", encoded);
// Stored value: "EXPIRY:1720000000000000:abc123"
```

### Getting an expired key:
```cpp
std::optional<std::string> LSMEngine::check_ttl(
    const std::string& key, const std::string& value) {

    // Check if value has TTL prefix
    if (value.substr(0, 7) != "EXPIRY:") {
        return value;  // No TTL — return as-is
    }

    // Parse the expiry timestamp
    auto colon1 = value.find(':', 7);
    auto expiry_str = value.substr(7, colon1 - 7);
    auto actual_value = value.substr(colon1 + 1);

    // Check if expired
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    if (std::stoull(expiry_str) < now) {
        // EXPIRED! Lazily tombstone it
        del(key);           // Write tombstone
        return std::nullopt; // Return "not found"
    }

    return actual_value;  // Still valid — return "abc123"
}
```

---

## 7.6 File Layout on Disk

```
vaultdb/
├── data/
│   ├── wal.bin          ← Write-Ahead Log (binary, append-only)
│   ├── 0001.sst         ← SSTable (sorted, binary)
│   ├── 0002.sst         ← Newer SSTable
│   └── 0003.sst         ← Newest SSTable
```

Each SSTable is immutable — once written, it's never modified. New data goes to new SSTables. Compaction creates a new merged file and deletes the old ones.
