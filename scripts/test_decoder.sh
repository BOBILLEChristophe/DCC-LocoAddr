#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/dcc-locoaddr-test"
mkdir -p "$BUILD_DIR"

g++ -std=c++17 -Wall -Wextra -pedantic \
    -I"$ROOT/src" \
    "$ROOT/extras/tests/test_decoder.cpp" \
    "$ROOT/src/LocoAddrDecoder.cpp" \
    -o "$BUILD_DIR/test_decoder"

"$BUILD_DIR/test_decoder"
