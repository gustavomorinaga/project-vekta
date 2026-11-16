#!/usr/bin/env bash
# Builds and runs test_edat_seek.c against the patched tree (patch 4015). The Windows side, where the bug was, runs in
# tools/wine-host-tests.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
grep -q edat_seek "$P/libs/filesystem/edat.c" 2>/dev/null \
    || { echo "FAIL: no edat_seek in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -w -I "$P/include" -I "$P/libs/filesystem/tests" \
    "$ROOT/tools/tests/test_edat_seek.c" -o "$out/t"
timeout 60 "$out/t" "$out"
