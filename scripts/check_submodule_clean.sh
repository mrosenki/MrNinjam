#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCK_FILE="${ROOT_DIR}/dependencies.lock"

unset GIT_INDEX_FILE GIT_DIR GIT_WORK_TREE

while IFS='|' read -r name repository expected_commit relative_path; do
  case "$name" in ''|'#'*) continue ;; esac
  checkout="${ROOT_DIR}/${relative_path}"
  if ! git -C "$checkout" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'ERROR: locked dependency is missing or is not a Git checkout: %s\n' "$relative_path" >&2
    exit 1
  fi
  actual_commit="$(git -C "$checkout" rev-parse HEAD)"
  if [ "$actual_commit" != "$expected_commit" ]; then
    printf 'ERROR: %s is at %s; expected %s.\n' "$name" "$actual_commit" "$expected_commit" >&2
    exit 1
  fi
  actual_repository="$(git -C "$checkout" remote get-url origin 2>/dev/null || true)"
  if [ "$actual_repository" != "$repository" ]; then
    printf 'ERROR: %s origin is %s; expected %s.\n' "$name" "$actual_repository" "$repository" >&2
    exit 1
  fi
  dirty_status="$(git -C "$checkout" status --porcelain --untracked-files=normal)"
  if [ -n "$dirty_status" ]; then
    printf 'ERROR: %s contains local modifications:\n%s\n' "$name" "$dirty_status" >&2
    exit 1
  fi
  if [ -f "${checkout}/.gitmodules" ]; then
    nested_status="$(git -C "$checkout" submodule status --recursive)"
    if printf '%s\n' "$nested_status" | grep -Eq '^[+-U]'; then
      printf 'ERROR: %s nested dependencies are missing or differ from the pinned tree:\n%s\n' "$name" "$nested_status" >&2
      exit 1
    fi
  fi
done < "$LOCK_FILE"

printf 'PASS: locked dependency checkouts are pinned and clean.\n'
