# VaultDB — Study Guide

This folder contains detailed documentation to help you understand every C++ concept, design pattern, and systems programming technique used in VaultDB.

**Who is this for?**
- If you know basic C++ with STL (vectors, maps, strings) and solve DSA problems, but haven't built a real systems project before — start with `01_cpp_beyond_dsa.md`.

## Reading Order

| # | File | What You'll Learn |
|---|------|-------------------|
| 1 | [C++ Beyond DSA](01_cpp_beyond_dsa.md) | Namespaces, `::`, `#pragma once`, header/cpp split, `static`, `const`, `explicit` |
| 2 | [Memory & Smart Pointers](02_memory_and_pointers.md) | `unique_ptr`, `shared_ptr`, RAII, why we don't use `new`/`delete` |
| 3 | [Concurrency & Threading](03_concurrency.md) | `std::mutex`, `std::lock_guard`, `std::atomic`, `std::thread`, race conditions |
| 4 | [File I/O & Binary Data](04_file_io.md) | `fstream`, binary read/write, `reinterpret_cast`, endianness |
| 5 | [Networking & Sockets](05_networking.md) | TCP sockets, `socket()`, `bind()`, `listen()`, `accept()`, `select()` |
| 6 | [Data Structures Deep Dive](06_data_structures.md) | LRU Cache, LSM Tree, MemTable, SSTable, WAL — how and why |
| 7 | [Architecture Walkthrough](07_architecture.md) | Full request flow: client → server → parser → engine → disk |
| 8 | [Interview Questions](08_interview_questions.md) | 30+ questions an interviewer might ask about this project |
