#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCK_FILE="${ROOT_DIR}/dependencies.lock"

if [ ! -f "$LOCK_FILE" ]; then
  printf 'ERROR: dependency lock file not found: %s\n' "$LOCK_FILE" >&2
  exit 1
fi

validate_existing_checkout() {
  local name="$1" repository="$2" commit="$3" destination="$4"
  if ! git -C "$destination" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'ERROR: %s exists but is not a Git checkout: %s\n' "$name" "$destination" >&2
    return 1
  fi
  local actual_commit actual_repository dirty_status
  actual_commit="$(git -C "$destination" rev-parse HEAD)"
  actual_repository="$(git -C "$destination" remote get-url origin 2>/dev/null || true)"
  dirty_status="$(git -C "$destination" status --porcelain --untracked-files=normal)"
  if [ "$actual_commit" != "$commit" ]; then
    printf 'ERROR: %s is at %s; expected %s. Existing checkouts are never reset.\n' "$name" "$actual_commit" "$commit" >&2
    return 1
  fi
  if [ "$actual_repository" != "$repository" ]; then
    printf 'ERROR: %s origin is %s; expected %s.\n' "$name" "$actual_repository" "$repository" >&2
    return 1
  fi
  if [ -n "$dirty_status" ]; then
    printf 'ERROR: %s contains local modifications:\n%s\n' "$name" "$dirty_status" >&2
    return 1
  fi
}

prepare_checkout() {
  local name="$1" repository="$2" commit="$3" relative_path="$4"
  local destination="${ROOT_DIR}/${relative_path}"
  case "$relative_path" in
    deps/*|external/*) ;;
    *) printf 'ERROR: unsafe dependency path in lock file: %s\n' "$relative_path" >&2; return 1 ;;
  esac
  if [[ ! "$commit" =~ ^[0-9a-fA-F]{40}$ ]]; then
    printf 'ERROR: %s does not use a full 40-character commit ID.\n' "$name" >&2
    return 1
  fi
  if [ -e "$destination" ]; then
    validate_existing_checkout "$name" "$repository" "$commit" "$destination"
    printf 'Verified %s at %s\n' "$name" "$commit"
    return
  fi

  mkdir -p "$(dirname "$destination")"
  local temporary="${destination}.fetching.$$"
  if [ -e "$temporary" ]; then
    printf 'ERROR: temporary path already exists: %s\n' "$temporary" >&2
    return 1
  fi
  trap 'rm -rf -- "$temporary"' RETURN
  git clone --no-checkout "$repository" "$temporary"
  # Preserve the pinned repository blobs byte-for-byte on every platform.
  # In particular, Git for Windows otherwise converts text sources to CRLF,
  # which prevents the reviewed LF-authored dependency patches from applying.
  git -C "$temporary" config core.autocrlf false
  git -C "$temporary" checkout --detach "$commit"
  if [ -f "${temporary}/.gitmodules" ]; then
    git -C "$temporary" submodule update --init --recursive
  fi
  validate_existing_checkout "$name" "$repository" "$commit" "$temporary"
  mv "$temporary" "$destination"
  trap - RETURN
  printf 'Prepared %s at %s\n' "$name" "$commit"
}

while IFS='|' read -r name repository commit relative_path; do
  case "$name" in ''|'#'*) continue ;; esac
  # Remain safe when invoked from a checkout created before the lock file's
  # LF-only Git attribute was introduced.
  relative_path="${relative_path%$'\r'}"
  prepare_checkout "$name" "$repository" "$commit" "$relative_path"
done < "$LOCK_FILE"

"${ROOT_DIR}/scripts/check_submodule_clean.sh"
printf 'PASS: all locked dependencies are present, pinned, and clean.\n'
