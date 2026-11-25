#!/usr/bin/env bash
# Builds and runs test_pad_inject.c against the patched tree's libs/input/pad_inject.h (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/input/pad_inject.h" ]] || { echo "FAIL: no pad_inject.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/input" "$ROOT/tools/tests/test_pad_inject.c" -o "$out/t"
timeout 60 "$out/t"
