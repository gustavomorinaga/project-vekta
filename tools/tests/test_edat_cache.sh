#!/usr/bin/env bash
# Builds and runs test_edat_cache.c against the patched tree (tools/ps3recomp-patched.sh first): patch 4014's EDAT
# cache folder, short names and ready hook, on a synthetic EDAT.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
grep -q edat_set_ready_hook "$P/libs/filesystem/edat.h" 2>/dev/null \
    || { echo "FAIL: no edat_set_ready_hook in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'chmod -R u+w "$out"; rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -w -I "$P/include" -I "$P/libs/filesystem" \
    "$ROOT/tools/tests/test_edat_cache.c" -o "$out/t"
mkdir "$out/s"
timeout 60 "$out/t" "$out/s"
