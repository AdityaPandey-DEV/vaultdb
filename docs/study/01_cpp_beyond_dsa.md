# 1. C++ Beyond DSA — Language Features Used in VaultDB

If you've been doing DSA in C++ (using `vector`, `map`, `sort`, etc.), you already know the basics. But building a real project uses many features you never touch in competitive programming. This guide explains each one with examples from VaultDB.

---

## 1.1 Namespaces and `::`

### What you know from DSA:
```cpp
using namespace std;
cout << "Hello";
```

### What VaultDB does:
```cpp
namespace vaultdb {
    class WAL { ... };
    class MemTable { ... };
}
```

**Why?** Imagine two libraries both have a class called `Parser`. Without namespaces, the compiler can't tell them apart. Namespaces create a "folder" for your code.

**The `::` operator** means "go inside this namespace/class and access this thing":
```cpp
vaultdb::WAL wal("file.wal");       // Access WAL class inside vaultdb namespace
std::string name = "Aditya";        // Access string class inside std namespace
vaultdb::WALOp::SET                 // Access SET inside WALOp inside vaultdb
```

**In .cpp files**, we write:
```cpp
void WAL::append(...) { ... }
//   ^^^^
//   This means "the append function that belongs to the WAL class"
```

Without `WAL::`, the compiler thinks `append` is just a regular standalone function.

---

## 1.2 Header Files (.h) vs Source Files (.cpp)

### The Problem:
In DSA, you write everything in one file. But in a real project with 20+ files, if you put all code in `.h` files, every file that `#include`s it will recompile the entire code. This makes builds extremely slow.

### The Solution — Declaration vs Definition:

**Header file (`.h`)** — The "menu" — WHAT exists:
```cpp
// wal.h
class WAL {
public:
    void append(WALOp op, const std::string& key, const std::string& value);
    // ^^^^ Just the signature. No function body.
};
```

**Source file (`.cpp`)** — The "kitchen" — HOW it works:
```cpp
// wal.cpp
#include "wal.h"

void WAL::append(WALOp op, const std::string& key, const std::string& value) {
    // Actual implementation here
    file_.write(...);
}
```

**Analogy:** A restaurant menu (`.h`) tells you what dishes exist. The kitchen (`.cpp`) is where the cooking actually happens. Customers (other files) only need the menu.

---

## 1.3 `#pragma once`

```cpp
#pragma once  // <-- This line at the top of every .h file
```

**Problem:** If `lsm_engine.h` includes `wal.h`, and `main.cpp` includes both `lsm_engine.h` and `wal.h`, then `wal.h` gets included TWICE. This causes "redefinition" errors.

**Solution:** `#pragma once` tells the compiler: "Only include this file once, even if multiple files try to include it."

**Old way (you might see in textbooks):**
```cpp
#ifndef WAL_H
#define WAL_H
// ... code ...
#endif
```
`#pragma once` does the same thing but is cleaner. All modern compilers support it.

---

## 1.4 `const` and `const&`

### `const` — "This value cannot change"
```cpp
size_t entry_count() const { return entry_count_; }
//                   ^^^^^
// This "const" means: calling this function will NOT modify the object.
// It's a promise: "I'm just reading, not writing."
```

### `const std::string&` — "Pass by reference, but don't modify"
```cpp
void append(WALOp op, const std::string& key, const std::string& value);
//                     ^^^^^             ^
//                     Can't modify      Pass by reference (no copy)
```

**Why not just `std::string key`?**
- `std::string key` → Creates a COPY of the string. If the key is 1000 characters, it copies all 1000 characters. Wasteful.
- `const std::string& key` → Passes a REFERENCE (like a pointer). No copy. The `const` prevents accidental modification.

**DSA analogy:** When you pass `vector<int>& v` to avoid copying the vector — same idea, but with `const` added for safety.

---

## 1.5 `explicit` Keyword

```cpp
explicit WAL(const std::string& path);
//^^^^^^^
```

**Without `explicit`:**
```cpp
WAL wal = "/tmp/data.wal";  // C++ silently converts string → WAL object. Surprise!
```

**With `explicit`:**
```cpp
WAL wal = "/tmp/data.wal";     // ❌ Compiler ERROR
WAL wal("/tmp/data.wal");      // ✅ You must explicitly construct it
```

**Why?** Prevents bugs from accidental type conversions. If a function expects a `WAL` object and you accidentally pass a string, `explicit` catches the mistake at compile time.

---

## 1.6 `static` Keyword (Multiple Meanings!)

### Static member function (in a class):
```cpp
class Parser {
public:
    static Command parse(const std::string& input);
    //^^^^
    // Can be called WITHOUT creating a Parser object:
    // Parser::parse("SET key value") ← No need for Parser p; p.parse(...)
};
```

**When to use:** When the function doesn't need any object state (no `this` pointer). It's like a utility function that belongs to the class logically but doesn't need an instance.

### Static variable (in a file):
```cpp
static const std::string TOMBSTONE = "__VAULTDB_TOMBSTONE__";
// This variable is only visible within this file (internal linkage)
```

---

## 1.7 `enum class` (Scoped Enumerations)

### Old C-style enum (bad):
```cpp
enum Color { RED, GREEN, BLUE };
enum TrafficLight { RED, GREEN, YELLOW };  // ❌ ERROR! RED already defined!
```

### Modern `enum class` (what VaultDB uses):
```cpp
enum class WALOp : uint8_t {
    SET = 1,
    DELETE = 2,
};
```

**Benefits:**
1. **Scoped:** `WALOp::SET` and `SomeOther::SET` don't clash.
2. **Type-safe:** Can't accidentally compare `WALOp::SET == 5`. Must cast explicitly.
3. **`: uint8_t`** — Stored as a single byte (saves space in binary WAL file).

---

## 1.8 `std::optional<T>` (C++17)

```cpp
std::optional<std::string> get(const std::string& key);
```

**Problem:** What should `get("nonexistent_key")` return?
- Return `""` (empty string)? But what if the user stored `""` as a value? Can't distinguish.
- Return `nullptr`? But `std::string` isn't a pointer.
- Throw an exception? Too heavy for a common operation.

**Solution:** `std::optional` is a container that either holds a value or holds nothing:
```cpp
auto result = memtable.get("key1");

if (result.has_value()) {
    std::cout << result.value();  // or *result
} else {
    std::cout << "Key not found";
}

// Shorthand:
if (result) { ... }   // has_value() check
```

**Return `std::nullopt`** to mean "no value":
```cpp
return std::nullopt;  // "I don't have an answer"
```

---

## 1.9 Constructor Initializer Lists

```cpp
// In DSA, you might write:
WAL(const std::string& path) {
    path_ = path;         // Assignment (2 steps: default-construct, then assign)
    entry_count_ = 0;
}

// In VaultDB, we write:
WAL::WAL(const std::string& path)
    : path_(path),        // Direct initialization (1 step: construct with value)
      entry_count_(0) {
    // constructor body
}
```

**Why the `: list` style?**
1. **Faster** for complex types — constructs directly instead of default-construct-then-assign.
2. **Required** for `const` members and reference members (can't assign to them, must initialize).
3. **Professional convention** — all production C++ uses this.

---

## 1.10 Deleted Functions

```cpp
class WAL {
    WAL(const WAL&) = delete;             // No copying
    WAL& operator=(const WAL&) = delete;  // No copy-assignment
};
```

**Why?** A WAL object holds an open file handle. If you copy a WAL:
- Both copies point to the same file
- When one is destroyed, it closes the file
- The other copy now has a dangling file handle → crash!

`= delete` makes the compiler refuse to compile any code that tries to copy it.

---

## 1.11 Trailing Underscores `_`

```cpp
class WAL {
private:
    std::string path_;       // <-- trailing underscore
    std::ofstream file_;
    size_t entry_count_ = 0;
};
```

This is a **naming convention** (not a language feature). It means "this is a private member variable." It prevents confusion:

```cpp
WAL(const std::string& path) : path_(path) {}
//                      ^^^^    ^^^^^
//                      parameter  member variable — no ambiguity!
```

Without the underscore, you'd have `path(path)` which is confusing.

---

## 1.12 `auto` Keyword

```cpp
auto result = cache_.get(key);  // Compiler figures out the type
// Same as: std::optional<std::string> result = cache_.get(key);
```

**When to use:** When the type is obvious from context or too long to write.
**When NOT to use:** When it makes the code unclear. VaultDB uses it sparingly.

---

## Summary: DSA C++ vs Systems C++

| Feature | DSA Usage | VaultDB Usage |
|---------|-----------|---------------|
| `namespace` | `using namespace std;` | Custom `vaultdb` namespace |
| Files | Single `main.cpp` | `.h` declarations + `.cpp` implementations |
| `const&` | Rarely used | Everywhere (performance) |
| `enum` | Basic enums | `enum class` with underlying type |
| Return "not found" | Return `-1` | `std::optional<T>` |
| Memory | Stack variables | RAII + smart pointers |
| Error handling | Return `-1` or print | `= delete`, `explicit`, type safety |
