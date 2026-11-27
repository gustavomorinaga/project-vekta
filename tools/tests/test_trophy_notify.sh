#!/usr/bin/env bash
# Builds and runs test_trophy_notify.c (sceNpTrophy's host API, patch 5002) against the patched tree
# (tools/ps3recomp-patched.sh first). A synthetic TRP in a temporary folder; no game data.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
grep -q ps3_trophy_set_notify "$P/libs/network/sceNpTrophy.h" 2>/dev/null ||
    { echo "FAIL: no ps3_trophy_set_notify in $P/libs/network/sceNpTrophy.h; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Wno-unused-function -ffunction-sections -fdata-sections \
    -I "$P/include" -I "$P/libs/network" \
    "$ROOT/tools/tests/test_trophy_notify.c" "$P/libs/system/cellSysutil.c" -Wl,--gc-sections -pthread -lm -o "$out/t"
timeout 60 "$out/t"
