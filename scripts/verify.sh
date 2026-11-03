#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="${ROOT_DIR}/build/${BUILD_TYPE}"
CLAP_ARTIFACT="${BUILD_DIR}/plugin/ninjamplugin_visage_spike.clap"
VST3_ARTIFACT="${BUILD_DIR}/plugin/ninjamplugin_visage_spike.vst3"

"${ROOT_DIR}/scripts/check_submodule_clean.sh"

printf "Configuring NinjamPlugin (%s)...\n" "$BUILD_TYPE"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DNINJAMPLUGIN_ENABLE_VISAGE_CLAP_EDITOR=ON \
  -DNINJAMPLUGIN_ENABLE_VISAGE_PROTOTYPE=ON \
  -DNINJAMPLUGIN_ENABLE_VST3=ON

printf "Building NinjamPlugin targets...\n"
cmake --build "$BUILD_DIR" -j 8

ctest --test-dir "$BUILD_DIR" -C "$BUILD_TYPE" --output-on-failure

for artifact in "$CLAP_ARTIFACT" "$VST3_ARTIFACT"; do
  if [ ! -e "$artifact" ]; then
    printf "ERROR: expected plugin artifact not found: %s\n" "$artifact" >&2
    exit 1
  fi
  printf "Plugin artifact: %s\n" "$artifact"
done

while IFS= read -r packaged_file; do
  cmake -DARTIFACT="$packaged_file" \
    -D"FORBIDDEN_PATHS=$ROOT_DIR" \
    -P "${ROOT_DIR}/scripts/check_artifact_paths.cmake"
done < <(find "${BUILD_DIR}/plugin" -type f)
