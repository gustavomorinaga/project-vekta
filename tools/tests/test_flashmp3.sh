#!/usr/bin/env bash
# Builds and runs test_flashmp3.c with port/kz1hd/native/flashmp3.c and minimp3 (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/runtime/spu/spu_context.h" ]] || { echo "FAIL: no patched tree at $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -w -I "$P/include" -I "$P/runtime/spu" -I "$P/runtime" -I "$P/runtime/platform" \
  -I "$ROOT/third_party/minimp3" "$ROOT/tools/tests/test_flashmp3.c" "$ROOT/port/kz1hd/native/flashmp3.c" -lm -o "$out/t"
timeout 120 "$out/t" "$ROOT/tools/tests/data/flashmp3_m1_32k_mono.l3" "$ROOT/tools/tests/data/flashmp3_m2_22k_mono.l3"
