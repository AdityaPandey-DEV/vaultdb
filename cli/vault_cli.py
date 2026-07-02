#!/usr/bin/env python3
"""
VaultDB Interactive CLI — vault-cli

A colorized terminal client for VaultDB, similar to redis-cli.
Connects via TCP to the VaultDB server and provides an interactive prompt.

Usage:
    python3 cli/vault_cli.py              # Connect to localhost:6379
    python3 cli/vault_cli.py -h host -p port
"""

import socket
import sys
import readline  # Enables up/down arrow history in input()

# ── ANSI Color Codes ──────────────────────────────────────────
GREEN  = "\033[92m"
YELLOW = "\033[93m"
RED    = "\033[91m"
CYAN   = "\033[96m"
BOLD   = "\033[1m"
DIM    = "\033[2m"
RESET  = "\033[0m"

BANNER = f"""
{CYAN}{BOLD} __      __         _ _   ____  ____
 \\ \\    / /        | | | |  _ \\|  _ \\
  \\ \\  / /_ _ _   _| | |_| | | | |_) |
   \\ \\/ / _` | | | | | __| | | |  _ <
    \\  / (_| | |_| | | |_| |_| | |_) |
     \\/ \\__,_|\\__,_|_|\\__|____/|____/{RESET}

{DIM}  Interactive CLI — Type HELP for commands{RESET}
"""

HELP_TEXT = f"""
{BOLD}Available Commands:{RESET}
  {GREEN}SET{RESET}  key value        Store a key-value pair
  {GREEN}GET{RESET}  key              Retrieve a value by key
  {GREEN}DEL{RESET}  key              Delete a key
  {GREEN}TTL{RESET}  key secs value   Store with Time-To-Live
  {GREEN}PING{RESET}                  Check server connection
  {GREEN}BENCH{RESET} [n]             Run n write operations (default 10000)
  {GREEN}STATS{RESET}                 Show engine statistics
  {YELLOW}HELP{RESET}                  Show this help message
  {RED}EXIT{RESET} / {RED}QUIT{RESET}           Disconnect and exit
"""


def colorize_response(response: str) -> str:
    """Apply colors to VaultDB server responses."""
    if response.startswith("OK"):
        return f"{GREEN}{BOLD}✓ {response}{RESET}"
    elif response.startswith("VALUE"):
        value = response[6:]  # Strip "VALUE " prefix
        return f"{YELLOW}→ {value}{RESET}"
    elif response.startswith("NULL"):
        return f"{DIM}(nil){RESET}"
    elif response.startswith("PONG"):
        return f"{GREEN}🏓 PONG{RESET}"
    elif response.startswith("STATS"):
        # Pretty-print stats
        parts = response[6:].split()
        lines = [f"\n{CYAN}{BOLD}📊 Engine Statistics{RESET}"]
        for part in parts:
            key, value = part.split("=")
            key_display = key.replace("_", " ").title()
            lines.append(f"  {DIM}│{RESET} {key_display}: {BOLD}{value}{RESET}")
        return "\n".join(lines)
    elif response.startswith("ERROR"):
        return f"{RED}✗ {response}{RESET}"
    else:
        return response


def connect(host: str, port: int) -> socket.socket:
    """Connect to VaultDB server."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
        sock.settimeout(5.0)
        return sock
    except ConnectionRefusedError:
        print(f"{RED}✗ Could not connect to VaultDB at {host}:{port}{RESET}")
        print(f"{DIM}  Make sure the server is running: ./build/vaultdb{RESET}")
        sys.exit(1)
    except Exception as e:
        print(f"{RED}✗ Connection error: {e}{RESET}")
        sys.exit(1)


def send_command(sock: socket.socket, command: str) -> str:
    """Send a command and receive the response."""
    try:
        sock.sendall((command + "\n").encode())
        data = sock.recv(4096).decode().strip()
        return data
    except socket.timeout:
        return "ERROR Timeout waiting for server response"
    except BrokenPipeError:
        return "ERROR Connection lost to server"


def main():
    host = "localhost"
    port = 6379

    # Parse command-line arguments
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] in ("-h", "--host") and i + 1 < len(args):
            host = args[i + 1]
            i += 2
        elif args[i] in ("-p", "--port") and i + 1 < len(args):
            port = int(args[i + 1])
            i += 2
        else:
            i += 1

    print(BANNER)

    sock = connect(host, port)

    # Verify connection with PING
    response = send_command(sock, "PING")
    if "PONG" in response:
        print(f"{GREEN}✓ Connected to VaultDB at {host}:{port}{RESET}\n")
    else:
        print(f"{RED}✗ Unexpected response: {response}{RESET}")
        sys.exit(1)

    # Interactive loop
    while True:
        try:
            cmd = input(f"{CYAN}{BOLD}vaultdb>{RESET} ").strip()

            if not cmd:
                continue

            upper = cmd.upper()

            if upper in ("EXIT", "QUIT", "Q"):
                print(f"\n{DIM}Goodbye! 👋{RESET}")
                break

            if upper == "HELP":
                print(HELP_TEXT)
                continue

            if upper == "CLEAR":
                print("\033[2J\033[H", end="")  # Clear terminal
                continue

            response = send_command(sock, cmd)
            print(colorize_response(response))

        except (KeyboardInterrupt, EOFError):
            print(f"\n{DIM}Goodbye! 👋{RESET}")
            break

    sock.close()


if __name__ == "__main__":
    main()
