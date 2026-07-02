# 🔒 VaultDB

**Redis-like persistent key-value store built from scratch in C++17.**

VaultDB is a systems programming project that implements a complete storage engine using the **Log-Structured Merge (LSM) Tree** architecture. Every component — from the LRU cache to the TCP server — is built from scratch using only the C++17 standard library, with no external dependencies except GoogleTest for testing.

The storage engine writes to a **Write-Ahead Log** for crash recovery, buffers data in a sorted **MemTable** (std::map), flushes to immutable **SSTables** on disk with **Bloom Filters** to skip unnecessary disk reads, and uses an **LRU cache** for hot key access. A background **compaction thread** merges SSTables to reduce read amplification.

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
| **Bloom Filter** | Probabilistic filter (FNV-1a + DJB2a, ~1.7% FPR). Skips disk reads. |
| **LSM Engine** | Coordinates all components. Background compaction thread. |
| **TCP Server** | select()-based non-blocking I/O. Redis-compatible port 6379. |
| **Protocol** | Text protocol: SET/GET/DEL/TTL/PING/BENCH/STATS commands. |
| **vault-cli** | Interactive Python CLI with colorized output for live demos. |
| **API Bridge** | Zero-dependency Python HTTP server bridging REST → TCP for the dashboard. |
| **Live Dashboard** | React dashboard with real-time polling, Bloom Filter visualizer, and web terminal. |
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
### Return to project root
```bash
cd ..
```

### Run
```bash
./build/vaultdb
# 🔒 VaultDB listening on port 6379
```

### Open New Terminal for Test and write
```bash
cd build
ctest --verbose
```

### Interactive CLI
```bash
# Terminal 1: Start VaultDB
./build/vaultdb

# Terminal 2: Connect with vault-cli (recommended)
python3 cli/vault_cli.py

# Or use raw netcat
nc localhost 6379
```

### Live Dashboard (Full-Stack Mode)
```bash
# Terminal 1: Start VaultDB
./build/vaultdb

# Terminal 2: Start API Bridge (connects React → C++ via HTTP → TCP)
python3 api/dashboard_api.py
# API Bridge running on http://localhost:5005

# Terminal 3: Start the React Dashboard
cd dashboard && npm run dev
# Open http://localhost:5173 in your browser
# Click "Live Mode: ON" to see real-time metrics!
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

## 🧪 Tests (24 GoogleTests)

| Test Suite | What It Verifies |
|------|-----------------|
| `LRUCacheTest` (5) | Eviction order, access updates, thread safety, update, remove |
| `WALTest` (2) | Binary append + recover, checkpoint truncation |
| `MemTableTest` (3) | CRUD + tombstones, size tracking, sorted entries |
| `SSTableTest` (3) | Flush/lookup, compaction merge, persistence across reload |
| `LSMEngineTest` (5) | Set/Get, Delete, Cache hits, WAL crash recovery, flush |
| `BloomFilterTest` (6) | Zero false negatives, FPR < 5%, empty filter, clear, memory, single element |

---

## 📚 What I Learned

- **LSM Trees** trade write amplification for read amplification — writes are always sequential (fast on SSDs), but reads may check multiple SSTables
- **WAL** provides durability without fsync on every write — the risk window is only the in-memory data since last WAL entry
- **Sparse indexes** on SSTables give near-O(log n) reads with much less memory than a full index
- **Compaction** is the key to keeping read performance stable as data grows
- **select()** is simpler than epoll but works cross-platform — good enough for < 1000 connections

---

## 🚀 Deployment & Interview Demo

VaultDB uses a **split deployment** strategy — the dashboard is always online for recruiters to visit, while the C++ engine runs locally to demonstrate systems-level skills.

### 1. Dashboard → Vercel (Free, Always Online)

The React dashboard is a **static site** that reads from a pre-generated `results.json`. No backend needed for the static view. It also includes an **Interactive Bloom Filter Visualizer** that runs entirely in the browser.

**Deploy in 3 steps:**
1. Go to [vercel.com](https://vercel.com) → Sign in with GitHub
2. Click **"Add New Project"** → Import `AdityaPandey-DEV/vaultdb`
3. Set **Root Directory** to `dashboard` → Click **Deploy**

That's it. Vercel auto-detects Vite and builds it. You'll get a URL like `vaultdb-dashboard.vercel.app` to share with recruiters.

> **Re-running benchmarks?** After `bash benchmark/run_benchmark.sh`, the script copies `results.json` into `dashboard/public/`. Commit and push → Vercel auto-redeploys with fresh data.

### 2. Full-Stack Live Demo → Local (Interview)

During an interview, run all 3 services locally to show the full-stack architecture:

```bash
# Terminal 1: Start the C++ Database Engine
./build/vaultdb

# Terminal 2: Start the Python API Bridge (HTTP → TCP)
python3 api/dashboard_api.py

# Terminal 3: Start the React Dashboard
cd dashboard && npm run dev
```

Now open `http://localhost:5173` in your browser:
- Click **"Live Mode: ON"** → the dashboard polls your C++ server every second for real-time stats
- Use the **Web Terminal** to type SET/GET/DEL commands directly in the browser
- Scroll to the **Bloom Filter Visualizer** to demonstrate how the probabilistic data structure works

### 3. Docker (Optional — Portable Demo)

```bash
docker build -t vaultdb .
docker run -p 6379:6379 vaultdb
# Now connect from another terminal: nc localhost 6379
```

### Interview Demo Flow (Recommended)

| Step | What to Show | Why It Impresses |
|------|-------------|-----------------|
| 1 | Open Vercel dashboard URL | "I built a full benchmark visualization pipeline" |
| 2 | Show GitHub repo + commit history | Clean, incremental commits show engineering discipline |
| 3 | Start `./build/vaultdb` + `python3 api/dashboard_api.py` + `npm run dev` | Full-stack: C++ → Python API → React frontend |
| 4 | Click **"Live Mode: ON"** in the dashboard | Real-time metrics prove the full-stack bridge works |
| 5 | Type `SET name aditya` in the **Web Terminal** | "I can interact with my C++ engine directly from the browser" |
| 6 | Run `BENCH 50000` via CLI or Web Terminal | Watch the live metrics cards jump in real-time |
| 7 | Show `STATS` → point out `bloom_saved` | "My Bloom Filter prevented X unnecessary disk reads" |
| 8 | Demo the **Bloom Filter Visualizer** | Type keys, watch bits light up — proves you understand the algorithm |
| 9 | Kill server, restart, `GET` old key | Demonstrate WAL crash recovery |
| 10 | Show test suite: `cd build && ctest` | 24 passing tests = engineering rigor |

---

## 📁 Project Structure

```
vaultdb/
├── src/
│   ├── engine/          # Core storage engine (LSM, MemTable, SSTable, WAL, Cache, Bloom Filter)
│   ├── protocol/        # TCP server + command parser
│   └── main.cpp         # Entry point
├── api/
│   └── dashboard_api.py # Python HTTP→TCP API Bridge (port 5005)
├── cli/
│   └── vault_cli.py     # Interactive Python CLI (Redis-like)
├── dashboard/           # React + Vite dashboard
│   └── src/
│       ├── App.jsx              # Main dashboard with Live Mode toggle
│       └── components/
│           ├── BloomFilterVisualizer.jsx  # Interactive Bloom Filter demo
│           └── WebTerminal.jsx           # Browser-based terminal
├── benchmark/           # Python benchmark client
├── tests/               # GoogleTest test suites (24 tests)
├── docs/study/          # C++ study guide for interview prep
└── CMakeLists.txt
```

---

*Built by Aditya Pandey · B.Tech CSE · GEHU*
