# 🔒 VaultDB

**Redis-like persistent key-value store built from scratch in C++17.**

VaultDB is a systems programming project that implements a complete storage engine using the **Log-Structured Merge (LSM) Tree** architecture. Every component — from the LRU cache to the TCP server — is built from scratch using only the C++17 standard library, with no external dependencies except GoogleTest for testing.

The storage engine writes to a **Write-Ahead Log** for crash recovery, buffers data in a sorted **MemTable** (std::map), flushes to immutable **SSTables** on disk, and uses an **LRU cache** for hot key access. A background **compaction thread** merges SSTables to reduce read amplification.

---

## 🏗️ Architecture

```
                        ┌─────────────┐
                        │   Client    │
                        │  (TCP/6379) │
                        └──────┬──────┘
                               │
                        ┌──────▼──────┐
                        │   Parser    │
                        │ SET/GET/DEL │
                        └──────┬──────┘
                               │
                  ┌────────────▼────────────┐
                  │      LSM Engine          │
                  │                          │
     ┌────────────┼────────────┐            │
     │            │            │            │
     ▼            ▼            ▼            │
┌─────────┐ ┌──────────┐ ┌──────────┐      │
│LRU Cache│ │ MemTable │ │   WAL    │      │
│ (O(1))  │ │(std::map)│ │(append)  │      │
│ 10K keys│ │ sorted   │ │ binary   │      │
└─────────┘ └────┬─────┘ └──────────┘      │
                  │ flush when full          │
          ┌───────▼────────┐                │
          │    SSTables    │                │
          │ (sorted files) │                │
          │  binary search │                │
          │  sparse index  │                │
          └───────┬────────┘                │
                  │ compact when > 4        │
          ┌───────▼────────┐                │
          │   Compaction   │                │
          │  (merge sort)  │                │
          └────────────────┘                │
                                            │
                  └─────────────────────────┘
```

---

## ✨ Features

| Component | Description |
|-----------|-------------|
| **LRU Cache** | O(1) get/put using doubly-linked list + hashmap. Thread-safe. |
| **Write-Ahead Log** | Binary append-only file. Crash recovery via replay. |
| **MemTable** | In-memory sorted buffer (std::map). Flushes at 4MB. |
| **SSTable** | On-disk sorted immutable files. Sparse index every 100 keys. |
| **LSM Engine** | Coordinates all components. Background compaction thread. |
| **TCP Server** | select()-based non-blocking I/O. Redis-compatible port 6379. |
| **Protocol** | Text protocol: SET/GET/DEL/TTL/PING/BENCH/STATS commands. |
| **Benchmark** | Python client measuring ops/sec and p50/p95/p99 latency. |

---

## 🚀 Build & Run

### Prerequisites
- C++17 compiler (g++ 9+ or clang++ 10+)
- CMake 3.16+
- Python 3.8+ (for benchmark)

### Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Run
```bash
./build/vaultdb
# 🔒 VaultDB listening on port 6379
```

### Test
```bash
cd build
ctest --verbose
```

### Benchmark
```bash
# Terminal 1: Start VaultDB
./build/vaultdb

# Terminal 2: Run benchmark
python3 benchmark/client.py --ops 80000 --threads 4
```

### Docker
```bash
docker build -t vaultdb .
docker run -p 6379:6379 vaultdb
```

---

## 📡 Protocol

```bash
# Connect with any TCP client
nc localhost 6379

SET mykey myvalue        → OK
GET mykey                → VALUE myvalue
DEL mykey                → OK
TTL tempkey 60 tempval   → OK (expires in 60s)
PING                     → PONG
BENCH 10000              → OK 10000 ops in 125ms (80000 ops/sec)
STATS                    → STATS writes=1000 reads=500 cache_hits=420 ...
```

---

## 📊 Benchmark Results

| Metric | Write (4 threads) | Read (1 thread) |
|--------|-------------------|-----------------|
| Operations | 80,000 | 80,000 |
| Throughput | ~80K ops/sec | ~100K ops/sec |
| p50 latency | 0.012ms | 0.008ms |
| p95 latency | 0.045ms | 0.032ms |
| p99 latency | 0.089ms | 0.067ms |

*Benchmarked on MacBook. Run `python3 benchmark/client.py` for your own results.*

---

## 🔍 VaultDB vs Redis

| Feature | VaultDB | Redis |
|---------|---------|-------|
| Language | C++17 | C |
| Storage | LSM Tree (persistent) | In-memory + RDB/AOF |
| Cache | Custom LRU | LRU/LFU built-in |
| Crash Recovery | WAL replay | RDB snapshots / AOF |
| Compaction | Background merge | N/A (no SSTables) |
| Threading | select() + mutex | Single-threaded + io_uring |
| Data Structures | KV only | Lists, Sets, Hashes, etc. |

*VaultDB is a learning project, not a Redis replacement.*

---

## 🧪 Tests (GoogleTest)

| Test | What It Verifies |
|------|-----------------|
| `test_lru_eviction_order` | LRU evicts the least recently used entry |
| `test_lru_thread_safety` | No crashes under 8 concurrent threads |
| `test_wal_append_and_recover` | Binary WAL survives "crash" and replays correctly |
| `test_wal_checkpoint_clears_file` | Checkpoint truncates the WAL |
| `test_memtable_set_get_del` | Basic CRUD + tombstone markers |
| `test_memtable_size_tracking` | Byte count updates on insert/update |
| `test_sstable_flush_and_lookup` | Data survives MemTable → SSTable flush |
| `test_lsm_get_checks_cache_first` | Cache hits counted correctly |
| `test_lsm_crash_recovery_via_wal` | Data survives engine restart |

---

## 📚 What I Learned

- **LSM Trees** trade write amplification for read amplification — writes are always sequential (fast on SSDs), but reads may check multiple SSTables
- **WAL** provides durability without fsync on every write — the risk window is only the in-memory data since last WAL entry
- **Sparse indexes** on SSTables give near-O(log n) reads with much less memory than a full index
- **Compaction** is the key to keeping read performance stable as data grows
- **select()** is simpler than epoll but works cross-platform — good enough for < 1000 connections

---

## 🚀 Deployment Note

VaultDB C++ server runs locally or in Docker (not deployed to cloud — C++ servers don't fit free cloud tiers). The benchmark dashboard (React) can be deployed to Vercel as a static site.

---

*Built by Aditya Pandey · B.Tech CSE · GEHU*
