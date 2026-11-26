#!/usr/bin/env bash
# Builds and runs test_temporal_hold.c against the patched tree's libs/video/rsx_temporal.c
# (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR/libs/video"
[[ -f "$P/rsx_temporal.c" ]] || { echo "FAIL: no $P/rsx_temporal.c; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P" "$ROOT/tools/tests/test_temporal_hold.c" "$P/rsx_temporal.c" -o "$out/t"
timeout 60 "$out/t"
