#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-Debug}"
BUILD_DIR="${ROOT_DIR}/build/${BUILD_TYPE}"

"${ROOT_DIR}/scripts/check_submodule_clean.sh"

printf "Configuring NinjamPlugin (%s)...\n" "$BUILD_TYPE"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DNINJAMPLUGIN_ENABLE_VST3=ON

printf "Building NinjamPlugin targets...\n"
cmake --build "$BUILD_DIR" -j 8

printf "Build complete: %s\n" "$BUILD_DIR"
