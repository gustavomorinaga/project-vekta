#!/usr/bin/env bash
# Builds and runs test_pad_test_input.c against the patched tree's libs/input/pad_test_input.h
# (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR/libs/input"
[[ -f "$P/pad_test_input.h" ]] || { echo "FAIL: no $P/pad_test_input.h; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P" "$ROOT/tools/tests/test_pad_test_input.c" -o "$out/t"
timeout 60 "$out/t"
