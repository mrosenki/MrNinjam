#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_TYPE="${1:-Release}"
SOURCE_BUILD_DIR="${2:-${ROOT_DIR}/build/${BUILD_TYPE}}"
INSTALLER="${ROOT_DIR}/scripts/install_development.sh"
TEST_ROOT="$(mktemp -d -t mrninjam-installer-test.XXXXXX)"
SYNTHETIC_BUILD_TYPE="InstallerSymlinkTest$$"
SYNTHETIC_BUILD_DIR="${ROOT_DIR}/build/${SYNTHETIC_BUILD_TYPE}"

cleanup() {
  rm -rf "$TEST_ROOT" "$SYNTHETIC_BUILD_DIR"
}
trap cleanup EXIT

CLAP_DIR="${TEST_ROOT}/clap"
VST3_DIR="${TEST_ROOT}/vst3"
mkdir -p "$CLAP_DIR" "$VST3_DIR"
touch "${CLAP_DIR}/NinjamPlugin.clap" "${VST3_DIR}/NinjamPlugin.vst3"

run_installer() {
  NINJAMPLUGIN_CLAP_INSTALL_DIR="$CLAP_DIR" \
  NINJAMPLUGIN_VST3_INSTALL_DIR="$VST3_DIR" \
  NINJAMPLUGIN_BUILD_DIR="$SOURCE_BUILD_DIR" \
    "$INSTALLER" "$BUILD_TYPE" "$@"
}

run_installer --dry-run --skip-verify
test ! -e "${CLAP_DIR}/MrNinjam.clap"
test ! -e "${VST3_DIR}/MrNinjam.vst3"

run_installer --skip-verify
run_installer --skip-verify
diff -rq "${SOURCE_BUILD_DIR}/plugin/MrNinjam.clap" \
  "${CLAP_DIR}/MrNinjam.clap"
diff -rq "${SOURCE_BUILD_DIR}/plugin/MrNinjam.vst3" \
  "${VST3_DIR}/MrNinjam.vst3"
test -e "${CLAP_DIR}/NinjamPlugin.clap"
test -e "${VST3_DIR}/NinjamPlugin.vst3"

mkdir -p "${SYNTHETIC_BUILD_DIR}/plugin"
ln -s "${SOURCE_BUILD_DIR}/plugin/MrNinjam.clap" \
  "${SYNTHETIC_BUILD_DIR}/plugin/MrNinjam.clap"
ln -s "${SOURCE_BUILD_DIR}/plugin/MrNinjam.vst3" \
  "${SYNTHETIC_BUILD_DIR}/plugin/MrNinjam.vst3"
if NINJAMPLUGIN_BUILD_DIR="$SYNTHETIC_BUILD_DIR" \
   "$INSTALLER" "$SYNTHETIC_BUILD_TYPE" --dry-run --skip-verify >/dev/null 2>&1; then
  printf 'ERROR: installer accepted symbolic-link artifacts.\n' >&2
  exit 1
fi

ln -s "$ROOT_DIR" "${TEST_ROOT}/source-link"
if NINJAMPLUGIN_CLAP_INSTALL_DIR="${TEST_ROOT}/source-link/generated-clap" \
   NINJAMPLUGIN_VST3_INSTALL_DIR="$VST3_DIR" \
   "$INSTALLER" "$BUILD_TYPE" --dry-run --skip-verify >/dev/null 2>&1; then
  printf 'ERROR: installer accepted a destination resolving into the source checkout.\n' >&2
  exit 1
fi
test ! -e "${ROOT_DIR}/generated-clap"

rm -rf "${CLAP_DIR}/MrNinjam.clap"
ln -s "${SOURCE_BUILD_DIR}/plugin/MrNinjam.clap" \
  "${CLAP_DIR}/MrNinjam.clap"
if run_installer --skip-verify >/dev/null 2>&1; then
  printf 'ERROR: installer replaced a symbolic-link target.\n' >&2
  exit 1
fi
test -L "${CLAP_DIR}/MrNinjam.clap"
rm "${CLAP_DIR}/MrNinjam.clap"

mkdir -p "${CLAP_DIR}/MrNinjam.clap" "${VST3_DIR}/MrNinjam.vst3"
printf 'verified-old-clap\n' > "${CLAP_DIR}/MrNinjam.clap/installed-marker"
printf 'verified-old-vst3\n' > "${VST3_DIR}/MrNinjam.vst3/installed-marker"
FAKE_CMAKE_DIR="${TEST_ROOT}/fake-cmake"
mkdir -p "$FAKE_CMAKE_DIR"
cat > "${FAKE_CMAKE_DIR}/cmake" <<'EOF'
#!/usr/bin/env bash
exit 37
EOF
chmod +x "${FAKE_CMAKE_DIR}/cmake"
if PATH="${FAKE_CMAKE_DIR}:${PATH}" run_installer >/dev/null 2>&1; then
  printf 'ERROR: verification failure did not stop installation.\n' >&2
  exit 1
fi
grep -q '^verified-old-clap$' "${CLAP_DIR}/MrNinjam.clap/installed-marker"
grep -q '^verified-old-vst3$' "${VST3_DIR}/MrNinjam.vst3/installed-marker"

REAL_MV="$(command -v mv)"
FAKE_MV_DIR="${TEST_ROOT}/fake-mv"
mkdir -p "$FAKE_MV_DIR"
cat > "${FAKE_MV_DIR}/mv" <<EOF
#!/usr/bin/env bash
if [[ "\$1" == *'.MrNinjam.vst3.install.'* && "\$2" == *'/MrNinjam.vst3' ]]; then
  exit 42
fi
exec "${REAL_MV}" "\$@"
EOF
chmod +x "${FAKE_MV_DIR}/mv"
if PATH="${FAKE_MV_DIR}:${PATH}" run_installer --skip-verify >/dev/null 2>&1; then
  printf 'ERROR: simulated commit failure unexpectedly succeeded.\n' >&2
  exit 1
fi
grep -q '^verified-old-clap$' "${CLAP_DIR}/MrNinjam.clap/installed-marker"
grep -q '^verified-old-vst3$' "${VST3_DIR}/MrNinjam.vst3/installed-marker"
if find "$TEST_ROOT" \( -name '*.install.*' -o -name '*.backup.*' \) -print | grep -q .; then
  printf 'ERROR: installer left transaction files after rollback.\n' >&2
  exit 1
fi

printf 'Development installer regression tests passed.\n'
