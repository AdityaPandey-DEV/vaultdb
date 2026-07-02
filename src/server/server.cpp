/**
 * VaultDB — TCP Server Implementation
 *
 * Uses select() for non-blocking I/O multiplexing. This works on both
 * macOS and Linux (unlike epoll which is Linux-only).
 *
 * select() limitations:
 *   - O(n) scanning of file descriptors each iteration
 *   - FD_SETSIZE limit (typically 1024 connections)
 *
 * For a learning project this is fine. Production systems would use
 * epoll (Linux) or kqueue (macOS) for O(1) event notification.
 */

#include "server.h"

namespace vaultdb {

Server::Server(LSMEngine& engine, int port)
    : engine_(engine), port_(port), running_(false), server_fd_(-1) {}

Server::~Server() {
    stop();
}

void Server::start() {
    // Create TCP socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "ERROR: Failed to create socket\n";
        return;
    }

    // Allow port reuse (avoids "address already in use" after restart)
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to address
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "ERROR: Failed to bind to port " << port_ << "\n";
        close(server_fd_);
        return;
    }

    // Listen with backlog of 128
    if (listen(server_fd_, 128) < 0) {
        std::cerr << "ERROR: Failed to listen\n";
        close(server_fd_);
        return;
    }

    // Set non-blocking so accept() doesn't block
    set_nonblocking(server_fd_);

    running_ = true;
    std::cout << "🔒 VaultDB listening on port " << port_ << "\n";

    // Main event loop using select()
    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd_, &read_fds);

        int max_fd = server_fd_;

        // Add all client file descriptors to the set
        for (const auto& [fd, conn] : connections_) {
            FD_SET(fd, &read_fds);
            max_fd = std::max(max_fd, fd);
        }

        // Timeout: 1 second (allows checking running_ flag periodically)
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (activity < 0) continue;

        // Check for new connections on the server socket
        if (FD_ISSET(server_fd_, &read_fds)) {
            accept_connection();
        }

        // Check existing clients for incoming data
        std::vector<int> to_remove;
        for (const auto& [fd, conn] : connections_) {
            if (FD_ISSET(fd, &read_fds)) {
                if (!handle_client(fd)) {
                    to_remove.push_back(fd);
                }
            }
        }

        // Remove disconnected clients
        for (int fd : to_remove) {
            ::close(fd);
            connections_.erase(fd);
        }
    }

    // Cleanup: close all connections
    for (const auto& [fd, conn] : connections_) ::close(fd);
    ::close(server_fd_);
}

void Server::stop() {
    running_ = false;
}

void Server::accept_connection() {
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);

    if (client_fd < 0) return;

    set_nonblocking(client_fd);
    connections_.emplace(client_fd, Connection(client_fd));
}

bool Server::handle_client(int fd) {
    char buf[4096];
    ssize_t bytes = recv(fd, buf, sizeof(buf) - 1, 0);

    if (bytes <= 0) return false;  // Disconnected or error

    auto it = connections_.find(fd);
    if (it == connections_.end()) return false;

    // Append raw bytes to the connection's buffer
    it->second.append_data(buf, static_cast<size_t>(bytes));

    // Extract and process all complete commands
    auto commands = it->second.extract_commands();
    for (const auto& line : commands) {
        auto cmd = Parser::parse(line);
        std::string response = Parser::execute(cmd, engine_);
        send(fd, response.c_str(), response.size(), 0);
    }

    return true;
}

void Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace vaultdb
