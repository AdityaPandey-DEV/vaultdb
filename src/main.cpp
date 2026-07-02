/**
 * VaultDB — Main Entry Point
 *
 * Redis-like persistent key-value store built from scratch in C++17.
 *
 * Components:
 *   LRU Cache → MemTable → SSTable → WAL → TCP Server
 *
 * Usage:
 *   ./vaultdb                    # Start on default port 6379
 *   VAULT_PORT=7777 ./vaultdb    # Custom port
 */

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "engine/lsm_engine.h"
#include "server/server.h"

// Global server pointer for signal handling
vaultdb::Server* g_server = nullptr;

void signal_handler(int signum) {
    std::cout << "\n🛑 Shutting down VaultDB...\n";
    if (g_server) g_server->stop();
}

int main() {
    // Read configuration from environment variables
    int port = 6379;
    std::string data_dir = "./data";
    size_t cache_size = 10000;
    size_t memtable_size_mb = 4;
    int compaction_interval = 60;

    if (const char* env = std::getenv("VAULT_PORT"))
        port = std::stoi(env);
    if (const char* env = std::getenv("VAULT_DATA_DIR"))
        data_dir = env;
    if (const char* env = std::getenv("VAULT_CACHE_SIZE"))
        cache_size = std::stoul(env);
    if (const char* env = std::getenv("VAULT_MEMTABLE_SIZE_MB"))
        memtable_size_mb = std::stoul(env);
    if (const char* env = std::getenv("VAULT_COMPACTION_INTERVAL_SEC"))
        compaction_interval = std::stoi(env);

    std::cout << R"(
 __      __         _ _   ____  ____
 \ \    / /        | | | |  _ \|  _ \
  \ \  / /_ _ _   _| | |_| | | | |_) |
   \ \/ / _` | | | | | __| | | |  _ <
    \  / (_| | |_| | | |_| |_| | |_) |
     \/ \__,_|\__,_|_|\__|____/|____/

)" << std::endl;

    std::cout << "📦 VaultDB v1.0.0 — Redis-like KV Store in C++17\n";
    std::cout << "   Port:               " << port << "\n";
    std::cout << "   Data directory:      " << data_dir << "\n";
    std::cout << "   Cache size:          " << cache_size << " entries\n";
    std::cout << "   MemTable threshold:  " << memtable_size_mb << " MB\n";
    std::cout << "   Compaction interval: " << compaction_interval << "s\n";
    std::cout << "───────────────────────────────────────────────\n";

    // Initialize LSM engine
    vaultdb::LSMEngine engine(data_dir, cache_size,
                               memtable_size_mb * 1024 * 1024,
                               compaction_interval);

    // Initialize TCP server
    vaultdb::Server server(engine, port);
    g_server = &server;

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start server (blocks until stop() is called)
    server.start();

    std::cout << "✅ VaultDB shut down cleanly.\n";
    return 0;
}
