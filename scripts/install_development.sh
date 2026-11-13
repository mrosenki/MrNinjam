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
  case "${destination}/" in
    "${ROOT_DIR}/"*)
      printf 'ERROR: refusing to install into the source checkout: %s\n' "$destination" >&2
      exit 1
      ;;
  esac
}

require_absolute_destination "$CLAP_INSTALL_DIR"
require_absolute_destination "$VST3_INSTALL_DIR"

BUILD_DIR="${ROOT_DIR}/build/${BUILD_TYPE}"
CLAP_ARTIFACT="${BUILD_DIR}/plugin/MrNinjam.clap"
VST3_ARTIFACT="${BUILD_DIR}/plugin/MrNinjam.vst3"

if [[ "$SKIP_VERIFY" -eq 0 ]]; then
  "${ROOT_DIR}/scripts/verify.sh" "$BUILD_TYPE"
fi

for artifact in "$CLAP_ARTIFACT" "$VST3_ARTIFACT"; do
  if [[ ! -e "$artifact" && ! -L "$artifact" ]]; then
    printf 'ERROR: expected development artifact not found: %s\n' "$artifact" >&2
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

install_artifact() {
  local source_path="$1"
  local install_dir="$2"
  local artifact_name="$3"
  local target_path="${install_dir}/${artifact_name}"

  if [[ "$DRY_RUN" -eq 1 ]]; then
    printf 'Would install: %s -> %s\n' "$source_path" "$target_path"
    return
  fi

  mkdir -p "$install_dir"
  local resolved_install_dir
  resolved_install_dir="$(cd "$install_dir" && pwd -P)"
  case "${resolved_install_dir}/" in
    "${ROOT_DIR}/"*)
      printf 'ERROR: refusing to install through a path that resolves into the source checkout: %s\n' \
        "$resolved_install_dir" >&2
      exit 1
      ;;
  esac

  target_path="${resolved_install_dir}/${artifact_name}"
  local temporary_path="${resolved_install_dir}/.${artifact_name}.install.$$"
  local backup_path="${resolved_install_dir}/.${artifact_name}.backup.$$"

  if [[ -e "$temporary_path" || -L "$temporary_path" ||
        -e "$backup_path" || -L "$backup_path" ]]; then
    printf 'ERROR: temporary install path already exists for %s\n' "$artifact_name" >&2
    exit 1
  fi

  if [[ -d "$source_path" ]]; then
    if ! cp -R "$source_path" "$temporary_path"; then
      rm -rf "$temporary_path"
      exit 1
    fi
  else
    if ! cp "$source_path" "$temporary_path"; then
      rm -f "$temporary_path"
      exit 1
    fi
  fi

  if [[ -e "$target_path" || -L "$target_path" ]]; then
    mv "$target_path" "$backup_path"
  fi

  if ! mv "$temporary_path" "$target_path"; then
    if [[ -e "$backup_path" || -L "$backup_path" ]]; then
      mv "$backup_path" "$target_path"
    fi
    exit 1
  fi

  if [[ -e "$backup_path" || -L "$backup_path" ]]; then
    rm -rf "$backup_path"
  fi

  printf 'Installed: %s\n' "$target_path"
}

install_artifact "$CLAP_ARTIFACT" "$CLAP_INSTALL_DIR" "MrNinjam.clap"
install_artifact "$VST3_ARTIFACT" "$VST3_INSTALL_DIR" "MrNinjam.vst3"

if [[ "$DRY_RUN" -eq 1 ]]; then
  printf 'Dry run complete; no files were changed.\n'
else
  printf 'Development install complete. Close and rescan the plug-ins in the DAW before testing.\n'
fi
