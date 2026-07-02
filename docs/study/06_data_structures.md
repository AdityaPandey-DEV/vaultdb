# 6. Data Structures Deep Dive

You know arrays, linked lists, trees, and hash maps from DSA. VaultDB combines them in clever ways. This guide explains each data structure, WHY it was chosen, and how it fits into the bigger picture.

---

## 6.1 LRU Cache — O(1) Get and Put

### What problem does it solve?
Reading from disk (SSTable) is ~1000x slower than reading from memory. An LRU cache keeps the most recently used keys in memory for instant access.

### What does LRU mean?
**Least Recently Used** — When the cache is full and we need to add a new entry, we evict the entry that hasn't been accessed for the longest time.

### How it works — Two data structures combined:

```
HashMap (unordered_map):           Doubly Linked List:
┌─────────┬──────────┐
│  "key1" │ → node1  │            HEAD ←→ node3 ←→ node1 ←→ node2 ←→ TAIL
│  "key2" │ → node2  │            (most recent)              (least recent)
│  "key3" │ → node3  │
└─────────┴──────────┘
```

**GET("key1"):**
1. HashMap lookup → O(1) → find node1
2. Move node1 to HEAD of linked list → O(1) (it was just accessed, so it's "recent")
3. Return value

**PUT("key4", "val4") when cache is full:**
1. Remove TAIL node from linked list (least recently used) → O(1)
2. Remove it from HashMap → O(1)
3. Add new node at HEAD → O(1)
4. Add to HashMap → O(1)

**Everything is O(1)!** That's why this combination is used everywhere (Redis, Chrome, Android, etc.)

### VaultDB's implementation:
```cpp
template <typename K, typename V>
class LRUCache {
    int capacity_;
    std::list<std::pair<K, V>> items_;  // Doubly linked list (std::list)
    std::unordered_map<K, typename std::list<std::pair<K,V>>::iterator> cache_;
    //                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //                     Maps key → iterator pointing into the linked list
    //                     This is what makes it O(1)!
};
```

**Why `std::list`?** In C++ STL, `std::list` is a doubly linked list. Its iterators remain valid even when other elements are added/removed (unlike `std::vector`).

**Why `template`?** So the cache can store any type: `LRUCache<string, string>` for VaultDB, `LRUCache<int, Page>` for a database buffer pool, etc.

---

## 6.2 MemTable — In-Memory Sorted Buffer

### What problem does it solve?
Writing directly to disk for every `SET` command would be extremely slow (disk seeks). Instead, we buffer writes in memory and flush to disk when the buffer is full (4MB).

### Why `std::map` (not `unordered_map`)?
```cpp
std::map<std::string, std::string> data_;
```

`std::map` keeps keys **sorted** (it's a Red-Black Tree internally).

**Why sorted matters:** When we flush MemTable to disk (SSTable), we need the keys in sorted order for binary search on disk. If we used `unordered_map`, we'd need to sort first (O(n log n)). With `std::map`, it's already sorted — flush is just O(n) sequential write.

### Performance comparison:

| Operation | `std::map` | `std::unordered_map` |
|-----------|-----------|---------------------|
| Insert | O(log n) | O(1) average |
| Lookup | O(log n) | O(1) average |
| Sorted iteration | O(n) ✅ | O(n log n) (must sort first) ❌ |

We accept slightly slower inserts (O(log n) vs O(1)) because the sorted flush is critical for SSTable performance.

### Tombstones (Soft Delete):
```cpp
static const std::string TOMBSTONE = "__VAULTDB_TOMBSTONE__";
```

When you `DEL key`, we don't actually remove it. We write a special "tombstone" value. Why?
- The key might exist in an older SSTable on disk
- If we just removed it from MemTable, a `GET` would find the old value in the SSTable
- The tombstone tells the system: "This key was explicitly deleted — ignore any older values"

---

## 6.3 Write-Ahead Log (WAL) — Crash Recovery

### What problem does it solve?
MemTable is in memory. If the server crashes, all in-memory data is lost. WAL ensures durability.

### The Write Path:
```
SET key value
    │
    ▼
1. Write to WAL (disk)     ← This survives a crash
    │
    ▼
2. Write to MemTable (RAM) ← This is lost on crash
    │
    ▼
3. If MemTable full:
   - Flush to SSTable (disk)
   - Clear MemTable
   - Checkpoint WAL (truncate)
```

### Why WAL works:
- Step 1 writes to disk FIRST (durability)
- If crash happens after step 1 but before step 2: we replay WAL on restart
- If crash happens after step 3: data is safely in SSTable, WAL is cleared

### WAL is append-only:
Unlike a database file where you seek and overwrite, WAL always writes at the end. This is the fastest possible disk operation — sequential writes are 100x faster than random writes on HDDs and even faster on SSDs.

---

## 6.4 SSTable — Sorted String Table (On-Disk)

### What problem does it solve?
MemTable has limited size (4MB). We need to persist data to disk. SSTables are the on-disk format.

### File format:
```
[key_len: 4B][key][val_len: 4B][value]  ← Entry 1
[key_len: 4B][key][val_len: 4B][value]  ← Entry 2
[key_len: 4B][key][val_len: 4B][value]  ← Entry 3
...
```
All entries are **sorted by key**. This enables binary search.

### Sparse Index — Memory-Efficient Lookup:

Storing a full index (every key → file offset) would use too much memory. Instead, we store every 100th key:

```
Sparse Index (in memory):
  "apple"     → offset 0
  "grape"     → offset 4500     ← 100th key
  "orange"    → offset 9200     ← 200th key
  "zebra"     → offset 14000    ← 300th key
```

**Looking up "mango":**
1. Binary search sparse index → "mango" is between "grape" (4500) and "orange" (9200)
2. Seek to offset 4500 in the file
3. Linear scan forward until we find "mango" (at most 100 entries)

**Complexity:** O(log(n/100) + 100) ≈ O(log n)

---

## 6.5 LSM Tree — The Full Architecture

**LSM = Log-Structured Merge Tree**

It's not a single tree — it's an ARCHITECTURE that combines all the components:

```
Level 0: MemTable (RAM)          ← Fast writes, limited size
         │ flush
Level 1: SSTable_newest (Disk)   ← Newest data
         SSTable_older (Disk)
         SSTable_oldest (Disk)   ← Oldest data
         │ compact (merge)
Level 2: SSTable_merged (Disk)   ← Compacted, deduplicated
```

### Write Amplification vs Read Amplification:

**Write amplification:** Data is written multiple times (MemTable → SSTable → Compacted SSTable). But each write is sequential, which is fast.

**Read amplification:** A GET might need to check MemTable + multiple SSTables. Compaction reduces this by merging SSTables.

### Compaction — Merging SSTables:

When there are > 4 SSTables, background compaction merges the two oldest:

```
Before compaction:
  SSTable_4 (newest)
  SSTable_3
  SSTable_2
  SSTable_1 (oldest) ← merge these two
  SSTable_0

After compaction:
  SSTable_4
  SSTable_3
  SSTable_2
  SSTable_merged (SSTable_0 + SSTable_1)
```

**Merge algorithm (like merge sort):**
1. Read all entries from both SSTables (already sorted)
2. Merge like in merge sort
3. If same key exists in both → keep the newer value
4. If value is TOMBSTONE → skip it (key was deleted)
5. Write merged entries to new SSTable

---

## 6.6 The Full Read Path

```
GET "mykey"
    │
    ▼
1. Check LRU Cache         → O(1)
   Found? Return it.
    │ (miss)
    ▼
2. Check MemTable           → O(log n)
   Found? Cache it. Return.
    │ (miss)
    ▼
3. Check SSTables           → O(log n) per table
   (newest to oldest)
   Found? Cache it. Return.
    │ (miss)
    ▼
4. Return NULL
```

**Why newest to oldest?** If a key was SET twice, the newest SSTable has the latest value. We stop at the first match.

---

## 6.7 Bloom Filter — Probabilistic Disk I/O Saver

### What problem does it solve?
When you `GET "key123"`, VaultDB checks each SSTable on disk. If there are 5 SSTables and the key only exists in the newest one, VaultDB still opens and reads the other 4 files for nothing. That's 4 wasted disk reads!

### What is a Bloom Filter?
A **space-efficient probabilistic data structure** that can answer:
- "Is this key **definitely NOT** in this SSTable?" → 100% accurate
- "Is this key **maybe** in this SSTable?" → Small chance of being wrong (~1.7%)

### How it works — Bit Array + Multiple Hash Functions:

```
Bloom Filter (bit array of 10,000 bits, initially all 0):
[0][0][0][0][0][0][0][0][0][0]...[0][0][0][0]

add("hello"):
  hash1("hello") % 10000 = 42    → set bit 42 to 1
  hash2("hello") % 10000 = 7831  → set bit 7831 to 1
  hash3("hello") % 10000 = 1203  → set bit 1203 to 1

[0]...[1]...[1]...[1]...[0]
       ^42   ^1203 ^7831

might_contain("hello"):
  Check bit 42   → 1 ✓
  Check bit 7831 → 1 ✓
  Check bit 1203 → 1 ✓
  All 3 are set → "MIGHT be present" ✓

might_contain("world"):
  Check bit 99   → 0 ✗
  At least one 0 → "DEFINITELY not present" ✓ (saved a disk read!)
```

### Why false positives happen:
Different keys can hash to the same bit positions. If "hello" sets bits 42, 1203, 7831, and later "cat" sets bits 42, 500, 7831, then checking for "dog" (which hashes to 42, 1203, 7831) would find all bits already set and incorrectly say "might contain." This is rare (~1.7%) and harmless — it just means we do a disk read that finds nothing.

### VaultDB's implementation:
```cpp
class BloomFilter {
    std::vector<bool> bits_;     // Compact bit array
    size_t num_bits_;            // Total number of bits

    // Three hash functions for independent bit positions
    size_t hash1(const std::string& key) const;  // FNV-1a
    size_t hash2(const std::string& key) const;  // FNV-1a (different seed)
    size_t hash3(const std::string& key) const;  // DJB2a
};
```

### Integration with SSTable:
```cpp
// On flush (writing MemTable to disk):
bloom_filter_ = BloomFilter(entries.size());
for (const auto& [key, value] : entries) {
    bloom_filter_.add(key);  // Add every key to the filter
}

// On get (reading from disk):
if (!bloom_filter_.might_contain(key)) {
    return std::nullopt;  // Skip disk I/O entirely!
}
// Only reach here if Bloom Filter says "maybe"
// Proceed with sparse index binary search + linear scan
```

### The math:
- **Bits per element (m/n):** 10 bits per key
- **Hash functions (k):** 3
- **False Positive Rate:** FPR ≈ (1 - e^(-kn/m))^k ≈ 1.7%
- **Memory:** 10,000 keys × 10 bits = 12.5 KB (tiny!)

### Impact on VaultDB:
With 5 SSTables and a random GET for a key that exists in only one:
- **Without Bloom Filter:** 5 file opens + 5 binary searches = slow
- **With Bloom Filter:** 1 file open + 1 binary search + 4 instant rejections = fast
- **Result:** ~80% fewer disk reads for cache-miss lookups

You can see the exact count by running `STATS` — the `bloom_saved` field shows how many disk reads were prevented.

---

## Summary

| Structure | Where | Why Chosen | Complexity |
|-----------|-------|-----------|------------|
| LRU Cache | Memory | O(1) hot key access | O(1) get/put |
| MemTable (std::map) | Memory | Sorted buffer for sequential flush | O(log n) insert |
| WAL | Disk | Crash recovery | O(1) append |
| SSTable | Disk | Sorted immutable file + sparse index | O(log n) lookup |
| Bloom Filter | Memory | Skip SSTables that don't have the key | O(1) check |
| LSM Tree | Architecture | Optimizes for write throughput | Amortized O(log n) |

