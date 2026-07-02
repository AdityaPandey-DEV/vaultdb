#!/bin/bash
# VaultDB — Benchmark Runner
#
# Builds VaultDB, starts the server, runs the benchmark, and saves results.
# Usage: bash benchmark/run_benchmark.sh
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "🔨 Building VaultDB..."
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Release -Wno-dev 2>&1 | tail -3
make -j4 2>&1 | tail -5

echo ""
echo "🚀 Starting VaultDB server..."
"$PROJECT_DIR/build/vaultdb" &
SERVER_PID=$!

# Wait for server to be ready
sleep 2

# Verify server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "❌ VaultDB failed to start"
    exit 1
fi

echo "📊 Running benchmark..."
cd "$SCRIPT_DIR"
python3 client.py --ops 80000 --threads 4

# Also copy results to dashboard for the React app
mkdir -p "$PROJECT_DIR/dashboard/public"
cp results.json "$PROJECT_DIR/dashboard/public/results.json"

echo ""
echo "🛑 Stopping VaultDB..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null || true

echo "✅ Benchmark complete! Results saved to benchmark/results.json and dashboard/public/results.json"
