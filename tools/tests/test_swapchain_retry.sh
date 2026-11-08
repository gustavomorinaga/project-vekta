#!/usr/bin/env bash
# Patch 2057: builds and runs test_swapchain_retry.c against the patched tree (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/rsx_swapchain_retry.h" ]] || { echo "FAIL: no rsx_swapchain_retry.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_swapchain_retry.c" -o "$out/t"
timeout 60 "$out/t"
