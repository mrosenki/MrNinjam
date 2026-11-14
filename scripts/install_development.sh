#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_TYPE="Release"
BUILD_TYPE_SET=0
DRY_RUN=0
SKIP_VERIFY=0

usage() {
  cat <<'EOF'
Usage: ./scripts/install_development.sh [BuildType] [--dry-run] [--skip-verify]

Verifies and installs MrNinjam CLAP and VST3 development bundles into the
standard per-user plug-in directories. Override the destinations for isolated
testing with NINJAMPLUGIN_CLAP_INSTALL_DIR and NINJAMPLUGIN_VST3_INSTALL_DIR.
EOF
}

for argument in "$@"; do
  case "$argument" in
    --dry-run)
      DRY_RUN=1
      ;;
    --skip-verify)
      SKIP_VERIFY=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      printf 'ERROR: unknown option: %s\n' "$argument" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ "$BUILD_TYPE_SET" -eq 1 ]]; then
        printf 'ERROR: only one build type may be supplied.\n' >&2
        exit 2
      fi
      BUILD_TYPE="$argument"
      BUILD_TYPE_SET=1
      ;;
  esac
done

if [[ ! "$BUILD_TYPE" =~ ^[A-Za-z0-9._-]+$ ]]; then
  printf 'ERROR: invalid build type: %s\n' "$BUILD_TYPE" >&2
  exit 2
fi

case "$(uname -s)" in
  Darwin)
    DEFAULT_CLAP_INSTALL_DIR="${HOME}/Library/Audio/Plug-Ins/CLAP"
    DEFAULT_VST3_INSTALL_DIR="${HOME}/Library/Audio/Plug-Ins/VST3"
    ;;
  Linux)
    DEFAULT_CLAP_INSTALL_DIR="${HOME}/.clap"
    DEFAULT_VST3_INSTALL_DIR="${HOME}/.vst3"
    ;;
  *)
    printf 'ERROR: use scripts/install_development.ps1 on Windows.\n' >&2
    exit 1
    ;;
esac

CLAP_INSTALL_DIR="${NINJAMPLUGIN_CLAP_INSTALL_DIR:-$DEFAULT_CLAP_INSTALL_DIR}"
VST3_INSTALL_DIR="${NINJAMPLUGIN_VST3_INSTALL_DIR:-$DEFAULT_VST3_INSTALL_DIR}"

require_absolute_destination() {
  local destination="$1"
  case "$destination" in
    /*) ;;
    *)
      printf 'ERROR: install destination must be absolute: %s\n' "$destination" >&2
      exit 1
      ;;
  esac
  case "/${destination#/}/" in
    */../*|*/./*)
      printf 'ERROR: install destination may not contain dot path components: %s\n' \
        "$destination" >&2
      exit 1
      ;;
  esac
  case "${destination}/" in
    "${ROOT_DIR}/"*)
      printf 'ERROR: refusing to install into the source checkout: %s\n' "$destination" >&2
      exit 1
      ;;
  esac
}

require_resolved_destination_outside_source() {
  local destination="$1"
  local existing_ancestor="$destination"
  local unresolved_suffix=""

  while [[ ! -d "$existing_ancestor" ]]; do
    local component
    component="$(basename "$existing_ancestor")"
    unresolved_suffix="${component}${unresolved_suffix:+/${unresolved_suffix}}"
    local parent
    parent="$(dirname "$existing_ancestor")"
    if [[ "$parent" == "$existing_ancestor" ]]; then
      printf 'ERROR: could not resolve install destination: %s\n' "$destination" >&2
      exit 1
    fi
    existing_ancestor="$parent"
  done

  local resolved_ancestor
  resolved_ancestor="$(cd "$existing_ancestor" && pwd -P)"
  local resolved_candidate="${resolved_ancestor}${unresolved_suffix:+/${unresolved_suffix}}"
  case "${resolved_candidate}/" in
    "${ROOT_DIR}/"*)
      printf 'ERROR: refusing an install path that resolves into the source checkout: %s\n' \
        "$destination" >&2
      exit 1
      ;;
  esac
}

require_absolute_destination "$CLAP_INSTALL_DIR"
require_absolute_destination "$VST3_INSTALL_DIR"
require_resolved_destination_outside_source "$CLAP_INSTALL_DIR"
require_resolved_destination_outside_source "$VST3_INSTALL_DIR"

BUILD_DIR="${NINJAMPLUGIN_BUILD_DIR:-${ROOT_DIR}/build/${BUILD_TYPE}}"
CLAP_ARTIFACT="${NINJAMPLUGIN_CLAP_ARTIFACT:-${BUILD_DIR}/plugin/MrNinjam.clap}"
VST3_ARTIFACT="${NINJAMPLUGIN_VST3_ARTIFACT:-${BUILD_DIR}/plugin/MrNinjam.vst3}"

if [[ "$SKIP_VERIFY" -eq 0 ]]; then
  NINJAMPLUGIN_BUILD_DIR="$BUILD_DIR" \
  NINJAMPLUGIN_CLAP_ARTIFACT="$CLAP_ARTIFACT" \
  NINJAMPLUGIN_VST3_ARTIFACT="$VST3_ARTIFACT" \
    "${ROOT_DIR}/scripts/verify.sh" "$BUILD_TYPE"
fi

for artifact in "$CLAP_ARTIFACT" "$VST3_ARTIFACT"; do
  if [[ ! -e "$artifact" ]]; then
    printf 'ERROR: expected development artifact not found: %s\n' "$artifact" >&2
    exit 1
  fi
  if [[ -L "$artifact" ]] || find "$artifact" -type l -print -quit | grep -q .; then
    printf 'ERROR: refusing symbolic links in development artifact: %s\n' "$artifact" >&2
    exit 1
  fi
  while IFS= read -r -d '' packaged_file; do
    cmake -DARTIFACT="$packaged_file" \
      -D"FORBIDDEN_PATHS=$ROOT_DIR" \
      -P "${ROOT_DIR}/scripts/check_artifact_paths.cmake"
  done < <(find "$artifact" -type f -print0)
done

warn_legacy_install() {
  local legacy_path="$1"
  if [[ -e "$legacy_path" || -L "$legacy_path" ]]; then
    printf 'WARNING: legacy development bundle retained at %s\n' "$legacy_path" >&2
    printf '         Remove it manually only after confirming old projects no longer need it.\n' >&2
  fi
}

warn_legacy_install "${CLAP_INSTALL_DIR}/NinjamPlugin.clap"
warn_legacy_install "${VST3_INSTALL_DIR}/NinjamPlugin.vst3"

if [[ "$DRY_RUN" -eq 1 ]]; then
  printf 'Would install: %s -> %s\n' \
    "$CLAP_ARTIFACT" "${CLAP_INSTALL_DIR}/MrNinjam.clap"
  printf 'Would install: %s -> %s\n' \
    "$VST3_ARTIFACT" "${VST3_INSTALL_DIR}/MrNinjam.vst3"
  printf 'Dry run complete; no files were changed.\n'
  exit 0
fi

mkdir -p "$CLAP_INSTALL_DIR" "$VST3_INSTALL_DIR"
require_resolved_destination_outside_source "$CLAP_INSTALL_DIR"
require_resolved_destination_outside_source "$VST3_INSTALL_DIR"

CLAP_INSTALL_DIR="$(cd "$CLAP_INSTALL_DIR" && pwd -P)"
VST3_INSTALL_DIR="$(cd "$VST3_INSTALL_DIR" && pwd -P)"

INSTALL_SOURCES=("$CLAP_ARTIFACT" "$VST3_ARTIFACT")
INSTALL_DIRS=("$CLAP_INSTALL_DIR" "$VST3_INSTALL_DIR")
INSTALL_NAMES=("MrNinjam.clap" "MrNinjam.vst3")
INSTALL_TARGETS=("" "")
INSTALL_TEMPS=("" "")
INSTALL_BACKUPS=("" "")
INSTALL_HAD_TARGET=(0 0)
INSTALL_COMMITTED=(0 0)
INSTALL_TRANSACTION_ACTIVE=1

rollback_install_transaction() {
  local index
  set +e
  for index in 0 1; do
    if [[ "${INSTALL_COMMITTED[$index]}" -eq 1 &&
          ( -e "${INSTALL_TARGETS[$index]}" || -L "${INSTALL_TARGETS[$index]}" ) ]]; then
      rm -rf "${INSTALL_TARGETS[$index]}"
    fi
  done
  for index in 0 1; do
    if [[ "${INSTALL_HAD_TARGET[$index]}" -eq 1 &&
          ( -e "${INSTALL_BACKUPS[$index]}" || -L "${INSTALL_BACKUPS[$index]}" ) ]]; then
      mv "${INSTALL_BACKUPS[$index]}" "${INSTALL_TARGETS[$index]}"
    fi
    if [[ -e "${INSTALL_TEMPS[$index]}" || -L "${INSTALL_TEMPS[$index]}" ]]; then
      rm -rf "${INSTALL_TEMPS[$index]}"
    fi
  done
  set -e
}

finish_install_transaction() {
  if [[ "$INSTALL_TRANSACTION_ACTIVE" -eq 1 ]]; then
    rollback_install_transaction
  fi
}
trap finish_install_transaction EXIT

for index in 0 1; do
  target_path="${INSTALL_DIRS[$index]}/${INSTALL_NAMES[$index]}"
  temporary_path="${INSTALL_DIRS[$index]}/.${INSTALL_NAMES[$index]}.install.$$"
  backup_path="${INSTALL_DIRS[$index]}/.${INSTALL_NAMES[$index]}.backup.$$"
  INSTALL_TARGETS[$index]="$target_path"
  INSTALL_TEMPS[$index]="$temporary_path"
  INSTALL_BACKUPS[$index]="$backup_path"

  if [[ -L "$target_path" ]]; then
    printf 'ERROR: refusing to replace symbolic-link install target: %s\n' "$target_path" >&2
    exit 1
  fi
  if [[ -e "$temporary_path" || -L "$temporary_path" ||
        -e "$backup_path" || -L "$backup_path" ]]; then
    printf 'ERROR: temporary install path already exists for %s\n' \
      "${INSTALL_NAMES[$index]}" >&2
    exit 1
  fi

  if [[ -d "${INSTALL_SOURCES[$index]}" ]]; then
    cp -R "${INSTALL_SOURCES[$index]}" "$temporary_path"
  else
    cp "${INSTALL_SOURCES[$index]}" "$temporary_path"
  fi
done

for index in 0 1; do
  if [[ -e "${INSTALL_TARGETS[$index]}" ]]; then
    mv "${INSTALL_TARGETS[$index]}" "${INSTALL_BACKUPS[$index]}"
    INSTALL_HAD_TARGET[$index]=1
  fi
done

for index in 0 1; do
  mv "${INSTALL_TEMPS[$index]}" "${INSTALL_TARGETS[$index]}"
  INSTALL_COMMITTED[$index]=1
done

INSTALL_TRANSACTION_ACTIVE=0
trap - EXIT

for index in 0 1; do
  if [[ -e "${INSTALL_BACKUPS[$index]}" ]]; then
    if ! rm -rf "${INSTALL_BACKUPS[$index]}"; then
      printf 'WARNING: installed successfully but could not remove backup: %s\n' \
        "${INSTALL_BACKUPS[$index]}" >&2
    fi
  fi
  printf 'Installed: %s\n' "${INSTALL_TARGETS[$index]}"
done

printf 'Development install complete. Close and rescan the plug-ins in the DAW before testing.\n'
