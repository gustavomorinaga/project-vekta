#!/usr/bin/env bash
# Builds and runs test_wall_clock.c against the patched tree (tools/ps3recomp-patched.sh first). The Windows side runs
# in tools/wine-host-tests.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/runtime/syscalls/lv2_wall_clock.h" ]] || { echo "FAIL: no lv2_wall_clock.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -w -I "$P/runtime/syscalls" "$ROOT/tools/tests/test_wall_clock.c" -o "$out/t"
timeout 60 "$out/t"
