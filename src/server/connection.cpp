/**
 * VaultDB — Connection Handler Implementation
 */

#include "connection.h"
#include <utility>

namespace vaultdb {

Connection::Connection(int fd) : fd_(fd) {}

Connection::~Connection() = default;

Connection::Connection(Connection&& other) noexcept
    : fd_(other.fd_), buffer_(std::move(other.buffer_)) {
    other.fd_ = -1;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        fd_ = other.fd_;
        buffer_ = std::move(other.buffer_);
        other.fd_ = -1;
    }
    return *this;
}

void Connection::append_data(const char* data, size_t len) {
    buffer_.append(data, len);
}

std::vector<std::string> Connection::extract_commands() {
    std::vector<std::string> commands;
    size_t pos;

    while ((pos = buffer_.find('\n')) != std::string::npos) {
        std::string line = buffer_.substr(0, pos);
        buffer_ = buffer_.substr(pos + 1);

        // Skip empty lines
        if (line.empty() || (line.size() == 1 && line[0] == '\r')) continue;

        // Remove trailing \r if present (handle \r\n line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!line.empty()) {
            commands.push_back(std::move(line));
        }
    }

    return commands;
}

}  // namespace vaultdb
