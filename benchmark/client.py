"""
VaultDB — Python Benchmark Client

Connects to VaultDB via TCP socket and measures performance:
- Write throughput (ops/sec)
- Read throughput (ops/sec)
- Latency percentiles (p50, p95, p99)

Usage:
    python3 client.py                     # Default: 10K ops
    python3 client.py --ops 80000 --threads 4
"""

import socket
import time
import threading
import statistics
import json
import argparse
import sys


class VaultDBClient:
    """Simple TCP client for VaultDB."""

    def __init__(self, host="localhost", port=6379):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))

    def _send(self, command: str) -> str:
        self.sock.sendall((command + "\n").encode())
        return self.sock.recv(4096).decode().strip()

    def set(self, key: str, value: str) -> str:
        return self._send(f"SET {key} {value}")

    def get(self, key: str) -> str:
        return self._send(f"GET {key}")

    def delete(self, key: str) -> str:
        return self._send(f"DEL {key}")

    def ping(self) -> str:
        return self._send("PING")

    def stats(self) -> str:
        return self._send("STATS")

    def close(self):
        self.sock.close()


def benchmark_writes(n=10000, threads=4, host="localhost", port=6379):
    """Benchmark SET operations across multiple threads."""
    print(f"\n📝 Write Benchmark: {n} operations, {threads} threads")
    print("─" * 55)

    ops_per_thread = n // threads
    latencies = []
    latency_lock = threading.Lock()
    errors = [0]

    def worker(thread_id, num_ops):
        try:
            client = VaultDBClient(host, port)
            local_latencies = []

            for i in range(num_ops):
                key = f"bench_w_{thread_id}_{i}"
                value = f"value_{thread_id}_{i}_{'x' * 50}"

                start = time.perf_counter()
                client.set(key, value)
                elapsed = (time.perf_counter() - start) * 1000  # ms

                local_latencies.append(elapsed)

            client.close()

            with latency_lock:
                latencies.extend(local_latencies)
        except Exception as e:
            errors[0] += 1
            print(f"  Thread {thread_id} error: {e}")

    start_time = time.perf_counter()

    thread_list = []
    for t in range(threads):
        th = threading.Thread(target=worker, args=(t, ops_per_thread))
        th.start()
        thread_list.append(th)

    for th in thread_list:
        th.join()

    total_time = time.perf_counter() - start_time
    actual_ops = len(latencies)
    ops_per_sec = actual_ops / total_time if total_time > 0 else 0

    if latencies:
        latencies.sort()
        p50 = latencies[int(len(latencies) * 0.50)]
        p95 = latencies[int(len(latencies) * 0.95)]
        p99 = latencies[int(len(latencies) * 0.99)]
    else:
        p50 = p95 = p99 = 0

    result = {
        "type": "write",
        "operations": actual_ops,
        "threads": threads,
        "duration_sec": round(total_time, 3),
        "ops_per_sec": round(ops_per_sec),
        "p50_ms": round(p50, 3),
        "p95_ms": round(p95, 3),
        "p99_ms": round(p99, 3),
        "errors": errors[0],
    }

    print(f"  Operations:  {actual_ops}")
    print(f"  Duration:    {total_time:.3f}s")
    print(f"  Throughput:  {ops_per_sec:,.0f} ops/sec")
    print(f"  Latency p50: {p50:.3f}ms")
    print(f"  Latency p95: {p95:.3f}ms")
    print(f"  Latency p99: {p99:.3f}ms")

    return result


def benchmark_reads(n=10000, host="localhost", port=6379):
    """Benchmark GET operations (single-threaded)."""
    print(f"\n📖 Read Benchmark: {n} operations")
    print("─" * 55)

    client = VaultDBClient(host, port)
    latencies = []

    # Pre-populate some keys
    for i in range(min(n, 1000)):
        client.set(f"bench_r_{i}", f"read_value_{i}")

    start_time = time.perf_counter()

    for i in range(n):
        key = f"bench_r_{i % 1000}"
        start = time.perf_counter()
        client.get(key)
        elapsed = (time.perf_counter() - start) * 1000
        latencies.append(elapsed)

    total_time = time.perf_counter() - start_time
    ops_per_sec = n / total_time if total_time > 0 else 0

    latencies.sort()
    p50 = latencies[int(len(latencies) * 0.50)]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]

    client.close()

    result = {
        "type": "read",
        "operations": n,
        "threads": 1,
        "duration_sec": round(total_time, 3),
        "ops_per_sec": round(ops_per_sec),
        "p50_ms": round(p50, 3),
        "p95_ms": round(p95, 3),
        "p99_ms": round(p99, 3),
    }

    print(f"  Operations:  {n}")
    print(f"  Duration:    {total_time:.3f}s")
    print(f"  Throughput:  {ops_per_sec:,.0f} ops/sec")
    print(f"  Latency p50: {p50:.3f}ms")
    print(f"  Latency p95: {p95:.3f}ms")
    print(f"  Latency p99: {p99:.3f}ms")

    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="VaultDB Benchmark Client")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--ops", type=int, default=10000)
    parser.add_argument("--threads", type=int, default=4)
    args = parser.parse_args()

    print("🔒 VaultDB Benchmark Client")
    print("=" * 55)

    # Test connection
    try:
        client = VaultDBClient(args.host, args.port)
        pong = client.ping()
        print(f"✅ Connected to VaultDB at {args.host}:{args.port} — {pong}")
        client.close()
    except Exception as e:
        print(f"❌ Cannot connect to VaultDB at {args.host}:{args.port}")
        print(f"   Error: {e}")
        print(f"   Make sure VaultDB is running: ./build/vaultdb")
        sys.exit(1)

    write_result = benchmark_writes(args.ops, args.threads, args.host, args.port)
    read_result = benchmark_reads(args.ops, args.host, args.port)

    # Save results as JSON for the dashboard
    results = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "config": {"ops": args.ops, "threads": args.threads},
        "write": write_result,
        "read": read_result,
    }

    with open("results.json", "w") as f:
        json.dump(results, f, indent=2)

    print(f"\n💾 Results saved to benchmark/results.json")
    print("=" * 55)
