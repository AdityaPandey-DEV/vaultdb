# 8. Interview Questions — What They Might Ask About VaultDB

These are 30+ questions an interviewer could ask about this project. For each question, there's a suggested answer that shows depth of understanding.

---

## Category 1: Architecture & Design

### Q1: "Why did you choose LSM Tree over B-Tree?"
**Answer:** LSM Trees are optimized for write-heavy workloads. All writes go to an in-memory buffer (MemTable) and WAL, making them sequential I/O — the fastest possible disk operation. B-Trees require random I/O for every write (seeking to the correct leaf node). Since VaultDB is designed for high-throughput writes (80K+ ops/sec), LSM was the better choice.

**Trade-off:** Reads can be slower (must check MemTable + multiple SSTables), but we mitigate this with an LRU cache and sparse indexes.

### Q2: "What happens if VaultDB crashes mid-write?"
**Answer:** We use a Write-Ahead Log. Every write goes to WAL (disk) BEFORE the MemTable (RAM). If the server crashes:
- Data written to WAL is safe on disk
- On restart, we replay the WAL to reconstruct the MemTable
- No data is lost

If the crash happens after flush but before WAL checkpoint, we might replay some already-flushed entries — but that's harmless since SET is idempotent (writing the same value twice is fine).

### Q3: "Why is the MemTable a std::map and not an unordered_map?"
**Answer:** std::map (Red-Black Tree) keeps keys sorted. When we flush MemTable to SSTable, we need sorted data for binary search on disk. With unordered_map, we'd need an O(n log n) sort before every flush. With std::map, iteration is already sorted — flush is O(n) sequential write.

### Q4: "How does your LRU cache achieve O(1)?"
**Answer:** It's a combination of two data structures:
- A doubly linked list for ordering (most recent at head, least recent at tail)
- A hash map mapping keys to list iterators

GET: HashMap lookup O(1) → move node to head O(1) → return value.
PUT: If full, remove tail O(1), remove from map O(1). Insert at head O(1), insert in map O(1).

### Q5: "What is compaction and why do you need it?"
**Answer:** Over time, we accumulate many SSTable files. Each GET must check all of them (newest to oldest). This is "read amplification." Compaction merges two SSTables into one, like merge sort:
- Deduplicates keys (keeps newer value)
- Removes tombstoned (deleted) keys
- Reduces the number of files a GET must scan

---

## Category 2: Concurrency & Threading

### Q6: "How do you handle concurrent clients?"
**Answer:** I use `select()` for I/O multiplexing. The main thread monitors all client sockets simultaneously. When data arrives on any socket, `select()` wakes up and we process that client's command. This avoids creating one thread per client (which is expensive).

### Q7: "Why did you use select() instead of epoll?"
**Answer:** `select()` works on both macOS and Linux, making the code portable. `epoll` is Linux-only. For a demo project handling <100 connections, `select()` is sufficient. In production, I'd use `epoll` on Linux for better scalability (O(1) vs O(n) per event).

### Q8: "What race conditions exist in your code?"
**Answer:** The main risk is concurrent access to the MemTable and SSTable list. I prevent this with:
- `engine_mutex_` in LSMEngine serializes all SET/GET/DEL/FLUSH operations
- `std::atomic<bool> running_` for safe communication with the compaction thread
- `std::lock_guard` ensures mutexes are always released (RAII pattern)

### Q9: "Could your code deadlock?"
**Answer:** No, because:
1. I use `std::lock_guard` which auto-releases on scope exit
2. There's only one mutex in the hot path (`engine_mutex_`), so no circular wait
3. The compaction thread also locks `engine_mutex_` but never holds two locks simultaneously

### Q10: "Why use std::atomic for counters instead of a mutex?"
**Answer:** Atomic operations use CPU hardware instructions (like `LOCK XADD`) which are ~10x faster than mutex lock/unlock (which involves OS kernel calls). For simple counters like `write_count_++`, atomic is the right choice. Mutex is for protecting multi-step operations.

---

## Category 3: Networking

### Q11: "Explain TCP vs UDP — why TCP for VaultDB?"
**Answer:** TCP provides:
- **Reliable delivery**: Every SET command must reach the server. UDP might lose packets.
- **Ordered delivery**: Commands must be processed in order (SET then GET, not reversed).
- **Connection state**: We can track per-client buffers.

UDP would be appropriate for metrics/logging where occasional loss is acceptable.

### Q12: "What is the TCP stream problem and how did you solve it?"
**Answer:** TCP is a byte stream, not a message stream. A single `recv()` might return half a command or two commands merged. I built a `Connection` class that buffers incoming bytes and extracts complete commands delimited by `\n`. It handles partial reads by accumulating data until a full command is available.

### Q13: "What is `htons` and why do you need it?"
**Answer:** `htons` = Host TO Network Short. Network protocols use big-endian byte order, but x86 CPUs use little-endian. `htons(6379)` converts the port number to the correct byte order. Without it, you'd bind to the wrong port.

---

## Category 4: C++ Specifics

### Q14: "Why use unique_ptr instead of raw pointers?"
**Answer:** RAII — unique_ptr automatically deletes the object when it goes out of scope. With raw pointers, you must remember to `delete`, and exceptions can cause memory leaks. VaultDB has zero `new`/`delete` calls — everything uses smart pointers.

### Q15: "What is RAII?"
**Answer:** Resource Acquisition Is Initialization. The idea is to tie resource lifetime to object lifetime:
- File handle opened in constructor, closed in destructor
- Mutex locked by lock_guard constructor, unlocked by destructor
- Memory allocated by make_unique, freed when unique_ptr is destroyed

This guarantees no resource leaks, even when exceptions occur.

### Q16: "Why did you split .h and .cpp files?"
**Answer:** Three reasons:
1. **Compilation speed**: Only changed .cpp files recompile, not everything that includes the header
2. **Encapsulation**: Users of the class see the interface (.h) without implementation details
3. **Professional standard**: This is how production C++ codebases are organized

### Q17: "Explain the const& in your function signatures"
**Answer:** `const std::string& key` passes by reference (avoids copying potentially large strings) and the `const` prevents the function from modifying the caller's data. Without `&`, every function call would copy the string. Without `const`, the function could accidentally modify the original.

---

## Category 5: Performance & Benchmarking

### Q18: "How did you benchmark your system?"
**Answer:** I wrote a Python client that:
1. Opens TCP connections to VaultDB
2. Spawns 4 threads, each firing 20,000 SET operations
3. Measures wall-clock time using `time.perf_counter()` (nanosecond precision)
4. Calculates throughput (ops/sec) and latency percentiles (p50, p95, p99)
5. Outputs results as JSON for the React dashboard

### Q19: "What do p50, p95, p99 latencies mean?"
**Answer:**
- **p50 (median)**: 50% of requests completed in under this time
- **p95**: 95% of requests completed in under this time
- **p99**: 99% of requests completed in under this time

p99 is the most important for user experience — it represents the "worst case" that 1 in 100 users will experience. A system with low p50 but high p99 has inconsistent performance.

### Q20: "What bottlenecks could occur at scale?"
**Answer:**
1. **Single mutex** — all operations are serialized. At high concurrency, threads would contend. Solution: reader-writer lock, or lock-free data structures.
2. **select() limit** — limited to ~1024 file descriptors. Solution: use epoll.
3. **Single-threaded I/O** — could use a thread pool for command processing.
4. **No compression** — SSTables store raw data. Adding Snappy/LZ4 compression would reduce disk I/O.

---

## Category 6: System Design & Trade-offs

### Q21: "If you had to scale VaultDB to handle 1 million ops/sec, what would you change?"
**Answer:**
1. Replace `select()` with `io_uring` (Linux async I/O)
2. Use a thread pool instead of single-threaded command processing
3. Implement a concurrent skip list instead of std::map (lock-free writes)
4. Add Bloom filters to SSTables (skip files that definitely don't have the key)
5. Implement leveled compaction (like LevelDB) instead of size-tiered

### Q22: "How does VaultDB compare to Redis?"
**Answer:** Redis stores everything in memory (fast but limited by RAM). VaultDB uses an LSM Tree (slower but can store far more data than RAM allows). Redis supports rich data types (lists, sets, sorted sets); VaultDB only supports key-value pairs. Redis is single-threaded with io_uring; VaultDB uses select() with a background compaction thread.

### Q23: "What would you add if you had more time?"
**Answer:**
1. **Bloom filters**: Probabilistic data structure to skip SSTables that don't contain a key
2. **Snappy compression**: Compress SSTable blocks to reduce disk usage
3. **Replication**: Replicate WAL to a secondary server for high availability
4. **Range queries**: Support `SCAN prefix*` by leveraging sorted MemTable/SSTable
5. **Prometheus metrics**: Export ops/sec, latency, cache hit rate for monitoring

---

## Category 7: Testing

### Q24: "How do you test crash recovery?"
**Answer:** In `test_lsm_engine.cpp`, I:
1. Create an LSMEngine instance and write data
2. Destroy the engine (simulating crash — destructor runs, MemTable lost)
3. Create a NEW engine instance pointing to the same data directory
4. Verify that all data is recoverable via WAL replay

### Q25: "What is GoogleTest and why did you choose it?"
**Answer:** GoogleTest is Google's C++ testing framework. I chose it because:
- Industry standard (used at Google, Meta, Microsoft)
- Supports test fixtures (`SetUp`/`TearDown`) for clean test isolation
- Provides rich assertions (`EXPECT_EQ`, `EXPECT_TRUE`, `ASSERT_*`)
- Integrates with CMake via FetchContent (no manual dependency management)

---

## Quick Answers for Common Questions

| Question | One-Line Answer |
|----------|----------------|
| "What language is VaultDB in?" | C++17, built with CMake, tested with GoogleTest |
| "How many lines of code?" | ~2000 lines of C++ across 14 files |
| "How many tests?" | 18 unit tests covering all components |
| "Is it production-ready?" | No — it's a learning project that demonstrates systems fundamentals |
| "What was the hardest part?" | Getting TCP stream handling right — partial reads broke everything initially |
| "How long did it take?" | Built incrementally over several focused coding sessions |
