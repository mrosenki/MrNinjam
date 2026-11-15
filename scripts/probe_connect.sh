#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-Release}"
TIMEOUT_SECONDS="${2:-45}"
BUILD_DIR="${ROOT_DIR}/build/${BUILD_TYPE}"
PROBE_BIN="${BUILD_DIR}/src/ninjamplugin_ninjam_bridge_live_connect_probe"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --target ninjamplugin_ninjam_bridge_live_connect_probe -j 8

printf "Running live connect probe (%s, timeout=%ss)\n" "$BUILD_TYPE" "$TIMEOUT_SECONDS"
"$PROBE_BIN" "$TIMEOUT_SECONDS"
