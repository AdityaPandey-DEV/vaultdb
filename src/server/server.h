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
 * Protocol: read until \n, parse command, write response.
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

namespace vaultdb {

class Server {
public:
    /**
     * Initialize the TCP server.
     * @param engine Reference to the LSM engine.
     * @param port Port to listen on (default 6379, same as Redis).
     */
    Server(LSMEngine& engine, int port = 6379)
        : engine_(engine), port_(port), running_(false), server_fd_(-1) {}

    ~Server() {
        stop();
    }

    /**
     * Start the server. Blocks until stop() is called.
     */
    void start() {
        // Create socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "ERROR: Failed to create socket\n";
            return;
        }

        // Allow port reuse
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // Bind
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "ERROR: Failed to bind to port " << port_ << "\n";
            close(server_fd_);
            return;
        }

        // Listen
        if (listen(server_fd_, 128) < 0) {
            std::cerr << "ERROR: Failed to listen\n";
            close(server_fd_);
            return;
        }

        // Set non-blocking
        set_nonblocking(server_fd_);

        running_ = true;
        std::cout << "🔒 VaultDB listening on port " << port_ << "\n";

        // Main event loop using select()
        while (running_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_fd_, &read_fds);

            int max_fd = server_fd_;

            for (int fd : client_fds_) {
                FD_SET(fd, &read_fds);
                max_fd = std::max(max_fd, fd);
            }

            // Timeout: 1 second (allows checking running_ flag)
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
            if (activity < 0) continue;

            // Check for new connections
            if (FD_ISSET(server_fd_, &read_fds)) {
                accept_connection();
            }

            // Check existing clients for data
            std::vector<int> to_remove;
            for (int fd : client_fds_) {
                if (FD_ISSET(fd, &read_fds)) {
                    if (!handle_client(fd)) {
                        to_remove.push_back(fd);
                    }
                }
            }

            // Remove disconnected clients
            for (int fd : to_remove) {
                close(fd);
                client_fds_.erase(
                    std::remove(client_fds_.begin(), client_fds_.end(), fd),
                    client_fds_.end());
                buffers_.erase(fd);
            }
        }

        // Cleanup
        for (int fd : client_fds_) close(fd);
        close(server_fd_);
    }

    /**
     * Stop the server gracefully.
     */
    void stop() {
        running_ = false;
    }

private:
    void accept_connection() {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);

        if (client_fd < 0) return;

        set_nonblocking(client_fd);
        client_fds_.push_back(client_fd);
        buffers_[client_fd] = "";
    }

    /**
     * Handle data from a client. Returns false if client disconnected.
     */
    bool handle_client(int fd) {
        char buf[4096];
        ssize_t bytes = recv(fd, buf, sizeof(buf) - 1, 0);

        if (bytes <= 0) return false;  // Disconnected or error

        buf[bytes] = '\0';
        buffers_[fd] += buf;

        // Process complete commands (delimited by \n)
        size_t pos;
        while ((pos = buffers_[fd].find('\n')) != std::string::npos) {
            std::string line = buffers_[fd].substr(0, pos);
            buffers_[fd] = buffers_[fd].substr(pos + 1);

            if (line.empty() || (line.size() == 1 && line[0] == '\r')) continue;

            // Remove trailing \r if present
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Parse and execute
            auto cmd = Parser::parse(line);
            std::string response = Parser::execute(cmd, engine_);

            // Send response
            send(fd, response.c_str(), response.size(), 0);
        }

        return true;
    }

    void set_nonblocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    LSMEngine& engine_;
    int port_;
    std::atomic<bool> running_;
    int server_fd_;
    std::vector<int> client_fds_;
    std::unordered_map<int, std::string> buffers_;  // Per-client read buffers
};

}  // namespace vaultdb
