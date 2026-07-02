#pragma once
/**
 * VaultDB — TCP Server
 *
 * Simple TCP server using select() for non-blocking I/O.
 * (select works on both macOS and Linux, unlike epoll which is Linux-only)
 *
 * Architecture:
 *   Main thread: accept connections, read/write via select()
 *   Each command is processed synchronously (LSM engine is thread-safe)
 *
 * Protocol: read until \n, pass to parser, write response.
 *
 * Uses the Connection class for per-client buffer management, which
 * handles TCP stream reassembly (since TCP is a byte stream, not a
 * message stream — a single recv() may contain partial commands).
 */

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>

// POSIX sockets
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include "../engine/lsm_engine.h"
#include "../protocol/parser.h"
#include "connection.h"

namespace vaultdb {

class Server {
public:
    /**
     * Initialize the TCP server.
     * @param engine Reference to the LSM engine.
     * @param port Port to listen on (default 6379, same as Redis).
     */
    Server(LSMEngine& engine, int port = 6379);
    ~Server();

    /**
     * Start the server. Blocks until stop() is called.
     */
    void start();

    /**
     * Stop the server gracefully.
     */
    void stop();

private:
    /** Accept a new client connection. */
    void accept_connection();

    /** Handle data from a client. Returns false if client disconnected. */
    bool handle_client(int fd);

    /** Set a file descriptor to non-blocking mode. */
    void set_nonblocking(int fd);

    LSMEngine& engine_;
    int port_;
    std::atomic<bool> running_;
    int server_fd_;
    std::unordered_map<int, Connection> connections_;  // fd → Connection
};

}  // namespace vaultdb
