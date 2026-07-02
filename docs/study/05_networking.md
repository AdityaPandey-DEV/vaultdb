# 5. Networking & Sockets — How VaultDB Talks Over TCP

VaultDB is a TCP server. Clients connect to it over the network (even on the same machine) and send commands like `SET key value`. This guide explains every networking concept used.

---

## 5.1 What is a Socket?

A **socket** is an endpoint for network communication. Think of it like a phone:
- **Server socket:** A phone number (waiting for calls)
- **Client socket:** Someone dialing that number

```
Client (Python benchmark)          Server (VaultDB C++)
┌──────────────────┐              ┌──────────────────┐
│ socket()         │              │ socket()         │
│ connect(6379)  ──┼──────────────┼→ bind(6379)      │
│                  │              │ listen()         │
│ send("SET k v")──┼──────────────┼→ accept()        │
│                  │              │ recv() → "SET k v"|
│ recv() ← "OK" ──┼──────────────┼← send("OK")      │
│ close()          │              │ close()          │
└──────────────────┘              └──────────────────┘
```

---

## 5.2 Server Socket Lifecycle (VaultDB's Flow)

### Step 1: `socket()` — Create the phone
```cpp
int server_fd = socket(AF_INET, SOCK_STREAM, 0);
// AF_INET     = IPv4 (Internet Protocol version 4)
// SOCK_STREAM = TCP (reliable, ordered byte stream)
// 0           = Default protocol for TCP
// Returns: file descriptor (an integer ID for this socket)
```

**What is a file descriptor?** In Unix, EVERYTHING is a "file" — regular files, sockets, pipes. Each gets an integer ID (0=stdin, 1=stdout, 2=stderr, 3+ = your stuff).

### Step 2: `setsockopt()` — Configure the phone
```cpp
int opt = 1;
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
// SO_REUSEADDR = Allow restarting the server immediately after stopping it
// Without this, you get "Address already in use" error for ~60 seconds
```

### Step 3: `bind()` — Assign a phone number (port)
```cpp
struct sockaddr_in address;
address.sin_family = AF_INET;           // IPv4
address.sin_addr.s_addr = INADDR_ANY;   // Accept connections from any IP
address.sin_port = htons(6379);          // Port 6379 (like Redis)

bind(server_fd, (struct sockaddr*)&address, sizeof(address));
```

**`htons(6379)`** = "host to network short" — converts the port number to **network byte order** (big-endian). Networks use big-endian; your Intel CPU uses little-endian. `htons` handles the conversion.

### Step 4: `listen()` — Start waiting for calls
```cpp
listen(server_fd, SOMAXCONN);
// SOMAXCONN = maximum pending connections in the queue (usually 128)
```

### Step 5: `accept()` — Answer the phone
```cpp
int client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
// Blocks until a client connects
// Returns: a NEW file descriptor for this specific client connection
// server_fd stays open to accept more clients
```

### Step 6: `recv()` and `send()` — Talk
```cpp
char buffer[4096];
int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
// Reads up to 4096 bytes from the client

send(client_fd, response.c_str(), response.size(), 0);
// Sends response back to the client
```

### Step 7: `close()` — Hang up
```cpp
close(client_fd);  // Close this client connection
close(server_fd);  // Stop accepting new connections
```

---

## 5.3 `select()` — Handling Multiple Clients

### The Problem:
`accept()` and `recv()` are **blocking** — they pause your program until data arrives. If you have 10 clients and one is slow, all others are stuck.

### Solution: `select()` — Watch multiple sockets at once

```cpp
fd_set read_fds;              // A set of file descriptors to watch
FD_ZERO(&read_fds);           // Clear the set
FD_SET(server_fd, &read_fds); // Watch the server socket (new connections)
FD_SET(client1_fd, &read_fds); // Watch client 1 (incoming data)
FD_SET(client2_fd, &read_fds); // Watch client 2

// Wait until ANY socket has data ready
int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

// Check which sockets are ready
if (FD_ISSET(server_fd, &read_fds)) {
    // New client connection! → accept()
}
if (FD_ISSET(client1_fd, &read_fds)) {
    // Client 1 sent data! → recv() and process
}
```

**Analogy:** Instead of standing next to one phone waiting for it to ring, `select()` is like a switchboard operator watching 100 phones and telling you which one is ringing.

### Why `select()` instead of `epoll`?
- `select()` works on **both macOS and Linux**
- `epoll` is Linux-only (better performance, but not portable)
- For VaultDB's scale (demo project, not production), `select()` is perfect

---

## 5.4 Non-blocking Sockets

```cpp
void Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);    // Get current flags
    fcntl(fd, F_SETFL, flags | O_NONBLOCK); // Add non-blocking flag
}
```

**Blocking socket:** `recv()` waits forever until data arrives.
**Non-blocking socket:** `recv()` returns immediately with `EAGAIN` if no data is available.

VaultDB uses non-blocking sockets with `select()`:
1. `select()` tells us which sockets have data
2. We only `recv()` from sockets that are ready
3. No blocking, no waiting

---

## 5.5 TCP is a Byte Stream (Not Messages!)

### The common mistake:
```
Client sends: "SET key1 value1\nGET key2\n"
```

You might expect `recv()` to give you `"SET key1 value1\n"` and then `"GET key2\n"` in separate calls. **WRONG!**

TCP is a **byte stream**. `recv()` might give you:
- `"SET key1 va"` (partial message!)
- `"lue1\nGET key2\n"` (rest of first + all of second)
- Or even all at once: `"SET key1 value1\nGET key2\n"`

### VaultDB's Solution — The Connection Class:

```cpp
class Connection {
    std::string buffer_;  // Accumulates bytes from recv()

public:
    void append(const char* data, size_t len) {
        buffer_.append(data, len);  // Add new bytes to the buffer
    }

    std::optional<std::string> extract_command() {
        auto pos = buffer_.find('\n');  // Look for a complete command
        if (pos == std::string::npos) {
            return std::nullopt;  // No complete command yet — wait for more data
        }
        std::string cmd = buffer_.substr(0, pos);  // Extract the command
        buffer_.erase(0, pos + 1);                   // Remove it from the buffer
        return cmd;
    }
};
```

**Flow:**
1. `recv()` gets bytes → `connection.append(bytes)`
2. `connection.extract_command()` checks if there's a full line
3. If yes → process it. If no → wait for more `recv()` calls.

---

## 5.6 `struct sockaddr_in` — Network Address

```cpp
struct sockaddr_in {
    sa_family_t    sin_family;  // AF_INET (IPv4)
    in_port_t      sin_port;    // Port number (network byte order)
    struct in_addr sin_addr;    // IP address
};
```

This is a C struct (not C++) — networking APIs are from the 1980s BSD Unix and use C conventions. That's why you see `(struct sockaddr*)` casts everywhere.

---

## 5.7 Port Numbers

- **Port 6379** = Redis's default port. VaultDB uses the same so Redis clients can connect.
- Ports 0-1023 = reserved (HTTP=80, HTTPS=443, SSH=22)
- Ports 1024-65535 = available for your applications

---

## Summary

| Function | What It Does | Analogy |
|----------|-------------|---------|
| `socket()` | Create a communication endpoint | Buy a phone |
| `bind()` | Assign address + port | Get a phone number |
| `listen()` | Start accepting connections | Turn the phone on |
| `accept()` | Accept one client | Answer a call |
| `recv()` | Read data from client | Listen to caller |
| `send()` | Write data to client | Talk to caller |
| `select()` | Watch multiple sockets at once | Switchboard operator |
| `close()` | End connection | Hang up |
| `htons()` | Convert port to network byte order | Translate language |
| `fcntl(O_NONBLOCK)` | Make socket non-blocking | Don't wait for call |
