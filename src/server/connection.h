#pragma once
/**
 * VaultDB — Per-Client Connection Handler
 *
 * Manages the state of a single TCP client connection:
 *   - Read buffer for accumulating partial commands
 *   - File descriptor for the socket
 *   - Methods to extract complete commands from the buffer
 *
 * WHY a separate class?
 * TCP is a byte stream, not a message stream. A single recv() call might
 * return half a command, or three-and-a-half commands. The Connection class
 * accumulates bytes until it finds a complete command (terminated by \n),
 * then extracts it from the buffer. This cleanly separates stream reassembly
 * from command processing.
 */

#include <string>
#include <vector>

namespace vaultdb {

class Connection {
public:
    /**
     * Create a connection for the given file descriptor.
     * @param fd The socket file descriptor for this client.
     */
    explicit Connection(int fd);
    ~Connection();

    // Non-copyable (each connection owns a unique socket)
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Movable (needed for std::unordered_map storage)
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    /**
     * Append received data to the internal buffer.
     * @param data Raw bytes received from the socket.
     * @param len Number of bytes.
     */
    void append_data(const char* data, size_t len);

    /**
     * Extract all complete commands from the buffer.
     * A command is complete when terminated by \n.
     * Handles \r\n line endings (strips trailing \r).
     *
     * @return Vector of complete command strings (without trailing \n or \r\n).
     */
    std::vector<std::string> extract_commands();

    /** Get the socket file descriptor. */
    int fd() const { return fd_; }

private:
    int fd_;
    std::string buffer_;
};

}  // namespace vaultdb
