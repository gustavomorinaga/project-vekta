#!/usr/bin/env bash
# Builds and runs test_nv0039_route.c against the patched tree (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/gcm_nv0039_route.h" ]] || { echo "FAIL: no gcm_nv0039_route.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_nv0039_route.c" -o "$out/t"
timeout 60 "$out/t"
