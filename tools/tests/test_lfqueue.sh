#!/usr/bin/env bash
# Builds and runs test_lfqueue.c against the patched tree (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/spurs/cellSpursLFQueue.c" ]] || { echo "FAIL: no libs/spurs/cellSpursLFQueue.c in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -w -I "$P/include" -I "$P/runtime" -I "$P/runtime/platform" -I "$P/libs" \
  "$ROOT/tools/tests/test_lfqueue.c" "$P/runtime/platform/win32_compat.c" -pthread -lm -o "$out/t"
timeout 120 "$out/t" "$ROOT/tools/tests/test_lfqueue_vectors.txt"
