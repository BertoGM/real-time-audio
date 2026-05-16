#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
echo "=== Building AudioStream ==="
cmake --build build -j"$(nproc)"
echo "=== Build complete ==="
echo " Server: ./build/audiostream_server"
echo " Client: ./build/audiostream_client"