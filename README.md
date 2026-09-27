# VaultDB

**Redis-compatible persistent key-value store built from scratch in C++17 using LSM-Tree architecture.**

![C++17](https://img.shields.io/badge/C++-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-064F8C?style=flat-square&logo=cmake&logoColor=white)
![Docker](https://img.shields.io/badge/Docker-2496ED?style=flat-square&logo=docker&logoColor=white)

---

## What It Does

VaultDB is a from-scratch storage engine implementing the **Log-Structured Merge-Tree** architecture. It speaks a Redis-compatible protocol on port 6379 and includes a live monitoring dashboard.

**Technical Highlights:**
- **LSM-Tree engine** — MemTable → SSTable flush → tiered compaction
- **Write-Ahead Log** — binary append-only with crash recovery
- **Bloom filters** — dual-hash (FNV-1a + DJB2a), ~1.7% false positive rate
- **LRU cache** — O(1) reads via doubly-linked list + hashmap
- **TCP server** — `select()`-based non-blocking I/O on port 6379
- **Live dashboard** — React UI with real-time stats and Bloom filter visualizer

## Architecture

```
Client (TCP/6379) → Parser → LSM Engine
                                ├── LRU Cache (O(1), 10K keys)
                                ├── MemTable (std::map, flush at 4MB)
                                ├── WAL (binary append, crash recovery)
                                ├── SSTables (sorted, sparse index)
                                └── Compaction (merge sort, triggered at >4 files)
```

## Tech Stack

| Component | Technology |
|---|---|
| Core Engine | C++17, CMake |
| Server | `select()`-based TCP |
| Dashboard | React |
| CLI | Python |
| Protocol | SET / GET / DEL / TTL / PING / BENCH / STATS |

## My Role

I designed the full storage architecture — LSM-Tree layering, WAL format, compaction strategy, cache eviction policy, and protocol design. Code generation was accelerated using AI tools; integration, debugging, and system-level decisions are mine.

## Quick Start

```bash
git clone https://github.com/AdityaPandey-DEV/vaultdb.git && cd vaultdb
mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
cd .. && ./build/vaultdb   # Listening on port 6379
```

## Project Structure

```
├── src/          Core C++ storage engine
├── api/          Python HTTP → TCP bridge
├── cli/          Interactive CLI
├── dashboard/    React monitoring UI
├── benchmark/    Performance tools
├── tests/        Test suites
└── docs/         Documentation
```

---

<div align="center">

*Architected & built by [Aditya Pandey](https://github.com/AdityaPandey-DEV) — AI-augmented development*

</div>
