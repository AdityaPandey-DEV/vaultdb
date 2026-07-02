# 3. Concurrency & Threading

VaultDB uses multi-threading for background compaction and must handle multiple clients simultaneously. This guide explains every concurrency primitive used.

---

## 3.1 Why Do We Need Threading?

**Without threading:**
```
Client sends SET → Server processes SET → Server sends OK → Client sends GET → ...
```
While processing SET, no other client can connect. If compaction (merging SSTables) takes 5 seconds, ALL clients are frozen for 5 seconds.

**With threading:**
- Main thread: handles client connections
- Background thread: runs compaction every 60 seconds
- Multiple clients can connect simultaneously

---

## 3.2 `std::thread` — Creating a Background Thread

```cpp
// In VaultDB's LSMEngine constructor:
compaction_thread_ = std::thread([this]() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::seconds(60)
        );
        if (running_) {
            compact();  // Merge SSTables in background
        }
    }
});
```

**Breaking this down:**

### `std::thread(function)` — Creates and starts a new thread
```cpp
std::thread t([]() {
    std::cout << "I'm running in a separate thread!";
});
```

### `[this]() { ... }` — Lambda (anonymous function)
```cpp
[this]()  {  ...  }
//^^^^      ^^^^
//capture   body
```
- `[this]` means "this lambda can access `this` pointer (i.e., the LSMEngine's members)"
- `()` means no parameters
- The body runs in the new thread

### `thread.join()` — Wait for thread to finish
```cpp
LSMEngine::~LSMEngine() {
    running_ = false;                     // Tell thread to stop
    if (compaction_thread_.joinable()) {   // Is the thread still running?
        compaction_thread_.join();          // BLOCK until thread finishes
    }
}
```

If you don't `join()`, the program may crash when the thread tries to access destroyed objects.

---

## 3.3 The Race Condition Problem

**What is a race condition?** Two threads accessing the same data simultaneously, causing corruption.

```cpp
// DANGER: Two threads doing this simultaneously
// Thread 1: memtable.set("key", "value1")
// Thread 2: memtable.set("key", "value2")

void set(const std::string& key, const std::string& value) {
    // Thread 1: reads current_size_ = 100
    // Thread 2: reads current_size_ = 100 (SAME value!)
    current_size_ -= old_size;
    // Thread 1: current_size_ = 100 - 10 = 90
    // Thread 2: current_size_ = 100 - 20 = 80  ← WRONG! Should be 70
    current_size_ += new_size;
    // CORRUPTED! Data is inconsistent.
}
```

---

## 3.4 `std::mutex` — The Lock

A **mutex** (mutual exclusion) is like a bathroom lock:
- Only ONE thread can hold the lock at a time
- Other threads wait (block) until the lock is released

```cpp
class MemTable {
private:
    std::map<std::string, std::string> data_;
    std::mutex mutex_;  // The lock
};
```

### Basic usage (WRONG way):
```cpp
void set(const std::string& key, const std::string& value) {
    mutex_.lock();       // Acquire the lock
    data_[key] = value;  // Safe: only this thread can access data_
    mutex_.unlock();     // Release the lock
    // PROBLEM: What if data_[key] throws an exception?
    // mutex_.unlock() never runs → DEADLOCK! All threads wait forever.
}
```

---

## 3.5 `std::lock_guard` — Safe Locking (RAII for Mutexes)

```cpp
// VaultDB's MemTable::set():
void MemTable::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    //             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    // Constructor locks the mutex
    // Destructor unlocks the mutex (even if exception thrown!)

    data_[key] = value;
}
// ← lock_guard destroyed here → mutex automatically unlocked
```

**Why `lock_guard` instead of manual `lock()`/`unlock()`?**
- `lock_guard` uses RAII — the mutex is ALWAYS released, even if an exception occurs
- You can never forget to unlock
- It's impossible to deadlock from a missed unlock

**Analogy:** `lock_guard` is like a self-locking bathroom door — it locks when you enter and automatically unlocks when you leave, even if you faint inside.

---

## 3.6 `std::atomic<T>` — Lock-Free Thread Safety

For simple values (numbers, booleans), a full mutex is overkill. `std::atomic` provides thread-safe reads/writes without a mutex:

```cpp
class LSMEngine {
    std::atomic<bool> running_{true};     // Thread-safe boolean
    std::atomic<size_t> write_count_{0};  // Thread-safe counter
    std::atomic<size_t> read_count_{0};
    std::atomic<size_t> cache_hits_{0};
};
```

### How it's used:
```cpp
// Main thread:
running_ = false;           // Atomic write — instantly visible to all threads

// Compaction thread:
while (running_) {           // Atomic read — sees the latest value
    // ...
}

// Counting operations:
write_count_++;              // Atomic increment — no race condition
```

### Atomic vs Mutex:

| Feature | `std::atomic` | `std::mutex` |
|---------|---------------|--------------|
| Speed | Very fast (hardware instruction) | Slower (OS kernel call) |
| Use case | Simple values (int, bool) | Complex operations (multiple steps) |
| Example | `counter++` | "Read map, modify, write back" |

**Rule of thumb:** Use `atomic` for single-variable updates. Use `mutex` when you need to protect multiple lines of code as one unit.

---

## 3.7 `std::this_thread::sleep_for()` — Pausing a Thread

```cpp
std::this_thread::sleep_for(std::chrono::seconds(60));
```

This pauses the current thread for 60 seconds without consuming CPU. Used for the compaction interval — check every 60 seconds if SSTables need merging.

**Note:** `std::chrono` is C++'s time library:
```cpp
std::chrono::seconds(60)          // 60 seconds
std::chrono::milliseconds(100)    // 100 milliseconds
std::chrono::hours(1)             // 1 hour
```

---

## 3.8 Thread Safety in VaultDB — The Full Picture

```
                    ┌─────────────────────┐
                    │    Main Thread       │
                    │  (TCP select loop)   │
                    └──────────┬──────────┘
                               │
              Calls set()/get()/del()
                               │
                    ┌──────────▼──────────┐
                    │    LSMEngine         │
                    │  engine_mutex_       │ ← Protects the entire engine
                    │                      │
                    │  ┌────────────────┐  │
                    │  │   MemTable     │  │
                    │  │  mutex_        │  │ ← Protects the std::map
                    │  └────────────────┘  │
                    │  ┌────────────────┐  │
                    │  │   WAL          │  │
                    │  │  mutex_        │  │ ← Protects the file handle
                    │  └────────────────┘  │
                    └──────────┬──────────┘
                               │
                    ┌──────────▼──────────┐
                    │  Compaction Thread   │
                    │  (runs every 60s)    │
                    │  Also locks          │
                    │  engine_mutex_       │
                    └─────────────────────┘
```

**Why multiple mutexes?**
- `engine_mutex_` in LSMEngine: ensures SET/GET/DEL/FLUSH/COMPACT are serialized
- `mutex_` in MemTable: would allow fine-grained locking (though currently the engine mutex covers it)
- `mutex_` in WAL: protects the file from concurrent writes

---

## Summary

| Concept | What It Does | VaultDB Usage |
|---------|-------------|---------------|
| `std::thread` | Run code in parallel | Background compaction |
| `std::mutex` | Lock to prevent race conditions | Protect MemTable, WAL, Engine |
| `std::lock_guard` | RAII wrapper — auto-unlock mutex | Every `set()`, `get()`, `del()` |
| `std::atomic` | Thread-safe simple values | `running_`, `write_count_`, counters |
| `thread.join()` | Wait for thread to finish | Destructor waits for compaction |
| Lambda `[this](){}` | Anonymous function with captures | Compaction thread body |
