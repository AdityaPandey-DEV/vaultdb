# 2. Memory Management & Smart Pointers

In DSA, you almost never think about memory. You create vectors, they get cleaned up automatically. But in systems programming, you need to understand exactly what happens in memory.

---

## 2.1 Stack vs Heap — Where Does Data Live?

### Stack (Automatic Memory):
```cpp
void process() {
    int x = 5;                     // Stack — auto-destroyed when function ends
    std::string name = "Aditya";   // Stack — auto-destroyed when function ends
    std::vector<int> v = {1,2,3};  // Stack — auto-destroyed when function ends
}
// ← x, name, v are all destroyed here automatically. No leaks!
```

**Stack is fast** (just move a pointer) but **limited in size** (~1-8 MB).

### Heap (Dynamic Memory):
```cpp
void process() {
    int* ptr = new int(42);        // Heap — YOU must delete it
    // ... use ptr ...
    delete ptr;                     // If you forget this → MEMORY LEAK
}
```

**Heap is unlimited** but **slow** (OS must find free memory) and **dangerous** (leaks, double-free, dangling pointers).

---

## 2.2 RAII — The Most Important C++ Concept

**RAII = Resource Acquisition Is Initialization**

The idea: **Tie a resource (memory, file, mutex) to an object's lifetime.** When the object is destroyed, the resource is automatically released.

```cpp
// RAII in action — VaultDB's WAL constructor:
WAL::WAL(const std::string& path) : path_(path) {
    std::filesystem::create_directories(
        std::filesystem::path(path_).parent_path()
    );
    file_.open(path_, std::ios::binary | std::ios::app);
    // ^^^^^ Resource acquired in constructor
}

WAL::~WAL() {
    if (file_.is_open()) {
        file_.close();
        // ^^^^^ Resource released in destructor
    }
}
```

**Why this matters:** You NEVER need to remember to close the file. When the `WAL` object goes out of scope (or the program ends), C++ guarantees the destructor runs.

**DSA analogy:** When you create a `vector<int> v`, you never call `v.free_memory()`. The vector's destructor does it automatically. RAII is the same principle applied to files, sockets, mutexes, etc.

---

## 2.3 `std::unique_ptr<T>` — "I Own This, And Only I"

```cpp
// VaultDB uses unique_ptr for SSTables:
std::vector<std::unique_ptr<SSTable>> sstables_;
```

### What it does:
```cpp
// Create an SSTable on the heap, owned by this unique_ptr
auto table = std::make_unique<SSTable>("data/0001.sst");

// Use it like a regular pointer
table->get("key1");

// When 'table' goes out of scope → SSTable is automatically deleted
// No memory leak, guaranteed!
```

### Why not a raw pointer?
```cpp
// BAD — raw pointer
SSTable* table = new SSTable("data/0001.sst");
// ... 200 lines of code ...
// Did you remember to: delete table; ?
// What if an exception was thrown on line 150? → MEMORY LEAK!

// GOOD — unique_ptr
auto table = std::make_unique<SSTable>("data/0001.sst");
// Even if an exception is thrown, unique_ptr's destructor runs → no leak!
```

### "Unique" means no sharing:
```cpp
auto a = std::make_unique<SSTable>("file.sst");
auto b = a;  // ❌ COMPILER ERROR! Can't copy a unique_ptr.
auto c = std::move(a);  // ✅ Transfer ownership. 'a' is now empty (nullptr).
```

### Where VaultDB uses it:
```cpp
// In LSMEngine — each SSTable is uniquely owned:
std::vector<std::unique_ptr<SSTable>> sstables_;

// Creating a new SSTable during flush:
auto new_table = std::make_unique<SSTable>(path);
new_table->flush(entries);
sstables_.push_back(std::move(new_table));  // Transfer into the vector
```

---

## 2.4 `std::move()` — Transfer Ownership Without Copying

### The Problem:
```cpp
std::string big_string(1000000, 'x');  // 1 million characters

std::string copy = big_string;         // COPIES all 1M characters. Slow!
std::string moved = std::move(big_string);  // MOVES the internal pointer. Fast!
// Now big_string is empty (""), and moved has the data
```

### How it works conceptually:
Instead of copying 1 million characters, `std::move` says: "Just take my internal pointer. I'll become empty." It's like handing someone your notebook instead of photocopying every page.

### Where VaultDB uses it:
```cpp
// When pushing SSTable into the vector:
sstables_.push_back(std::move(new_table));
// new_table is now nullptr — the vector owns the SSTable

// When returning compacted SSTable:
return std::make_unique<SSTable>(output_path);
// The unique_ptr is moved to the caller (no copy)
```

---

## 2.5 `reinterpret_cast` — Binary Data Conversion

```cpp
// Writing a uint32_t as 4 raw bytes to a binary file:
uint32_t key_len = key.size();
file_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
```

**What this does:**
- `&key_len` is a `uint32_t*` (pointer to a 4-byte integer)
- `file_.write()` expects a `const char*` (pointer to bytes)
- `reinterpret_cast<const char*>` says: "Treat these 4 bytes of integer as 4 raw characters"

**Analogy:** Imagine the number `258` stored as `uint32_t`:
```
Memory: [02] [01] [00] [00]  ← 4 bytes (little-endian)
```
`reinterpret_cast` lets us write these exact raw bytes to a file. When reading back, we do the reverse to reconstruct the number.

**WARNING:** `reinterpret_cast` is dangerous if misused. It tells the compiler "trust me, I know what I'm doing." VaultDB only uses it for binary file I/O where we need exact byte-level control.

---

## 2.6 Destructor (`~ClassName`)

```cpp
WAL::~WAL() {
    if (file_.is_open()) {
        file_.close();
    }
}

LSMEngine::~LSMEngine() {
    running_ = false;                    // Signal compaction thread to stop
    if (compaction_thread_.joinable()) {
        compaction_thread_.join();        // Wait for it to finish
    }
}
```

**The destructor is called automatically when:**
1. A stack object goes out of scope (`}`)
2. `delete` is called on a heap object
3. A `unique_ptr` is destroyed
4. The program exits normally

**In VaultDB:**
- `~WAL()` closes the log file
- `~LSMEngine()` stops the background compaction thread
- `~Server()` (implicitly) closes all sockets

---

## 2.7 Why VaultDB Never Uses `new` / `delete`

You won't find a single `new` or `delete` in VaultDB's code. Instead:

| Old Way | VaultDB Way |
|---------|-------------|
| `SSTable* t = new SSTable(...)` | `auto t = std::make_unique<SSTable>(...)` |
| `delete t;` | (automatic when unique_ptr is destroyed) |
| Manual file close | RAII in destructor |

This is modern C++ best practice. Raw `new`/`delete` is considered legacy code. `std::make_unique` is safer because it's impossible to leak memory.

---

## Summary

| Concept | One-line Explanation |
|---------|---------------------|
| RAII | Acquire in constructor, release in destructor — never leak |
| `unique_ptr` | Smart pointer that auto-deletes when it goes out of scope |
| `std::move` | Transfer ownership instead of copying — fast |
| `reinterpret_cast` | Treat bytes as a different type — for binary I/O |
| Destructor `~` | Cleanup function called automatically on object destruction |
| `make_unique` | Safe replacement for `new` — no manual `delete` needed |
