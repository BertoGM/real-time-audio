#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "=== Setting up AudioStream ==="

# ── miniaudio ──────────────────────────────────────────────────────────────
MINIAUDIO_URL="https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h"
MINIAUDIO_DST="include/miniaudio.h"
 
if [ ! -f "$MINIAUDIO_DST" ]; then
    echo "[1/3] Downloading miniaudio.h..."
    curl -fsSL "$MINIAUDIO_URL" -o "$MINIAUDIO_DST"
    echo "      -> $MINIAUDIO_DST"
else
    echo "[1/3] miniaudio.h already present, skipping."
fi
 
# ── test.wav ───────────────────────────────────────────────────────────────
if [ ! -f "test.wav" ]; then
    echo "[2/3] Generating test.wav (5 s sine wave at 440 Hz)..."
    if command -v ffmpeg &>/dev/null; then
        ffmpeg -f lavfi \
               -i "sine=frequency=440:sample_rate=48000:duration=5" \
               -ac 2 -ar 48000 -c:a pcm_s16le \
               test.wav -y -loglevel error
        echo "      -> test.wav"
    else
        echo "      [WARN] ffmpeg not found — skipping test.wav generation."
        echo "             Install it with: sudo apt install ffmpeg"
    fi
else
    echo "[2/3] test.wav already present, skipping."
fi
 
# ── CMake configure ────────────────────────────────────────────────────────
echo "[3/3] Configuring CMake (Debug)..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5
 
echo ""
echo "=== Setup complete! ==="
echo ""
echo "Build:   ./scripts/build.sh"
echo "Server:  ./build/audiostream_server --source=file:test.wav"
echo "Client:  ./build/audiostream_client --host=127.0.0.1"