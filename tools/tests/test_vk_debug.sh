#!/usr/bin/env bash
# Patch 2064: builds and runs test_vk_debug.c against the patched tree (tools/ps3recomp-patched.sh first): the parser
# of PS3RECOMP_VK_VALIDATION and the debug messenger's repeat filter, rsx_vk_debug.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/rsx_vk_debug.h" ]] || { echo "FAIL: no rsx_vk_debug.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_vk_debug.c" -o "$out/t"
timeout 60 "$out/t"
