#!/usr/bin/env bash
# Usage: ./scripts/test_local.sh [tcp|udp] [jitter_ms]
set -euo pipefail
cd "$(dirname "$0")/.."
 
PROTO="${1:-tcp}"
JITTER="${2:-80}"
WAV="test.wav"
 
if [ ! -f "$WAV" ]; then
    echo "ERROR: $WAV not found. Run ./setup.sh first."
    exit 1
fi
 
if [ ! -f "./build/audiostream_server" ] || [ ! -f "./build/audiostream_client" ]; then
    echo "ERROR: Binaries not found. Run ./scripts/build.sh first."
    exit 1
fi
 
echo "=== Loopback test: proto=$PROTO jitter=${JITTER}ms ==="
 
# Start server in background
./build/audiostream_server \
    --source=file:"$WAV" \
    --proto="$PROTO" \
    --port=9001 \
    --debug &
SERVER_PID=$!
 
# Give the server a moment to bind
sleep 0.3
 
# Start client
./build/audiostream_client \
    --host=127.0.0.1 \
    --proto="$PROTO" \
    --port=9001 \
    --jitter="$JITTER" \
    --debug &
CLIENT_PID=$!
 
cleanup() {
    echo ""
    echo "=== Stopping ==="
    kill "$CLIENT_PID" 2>/dev/null || true
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$CLIENT_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
 
echo "Server PID=$SERVER_PID  Client PID=$CLIENT_PID"
echo "Press Ctrl-C to stop."
wait "$CLIENT_PID"