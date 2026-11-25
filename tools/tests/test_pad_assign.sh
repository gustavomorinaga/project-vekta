#!/usr/bin/env bash
# Builds and runs test_pad_assign.c against the patched tree's libs/input/pad_assign.h
# (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR/libs/input"
[[ -f "$P/pad_assign.h" ]] || { echo "FAIL: no $P/pad_assign.h; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P" "$ROOT/tools/tests/test_pad_assign.c" -o "$out/t"
timeout 60 "$out/t"
