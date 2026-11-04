#!/usr/bin/env bash
# Builds and runs test_present_wait.c against the patched tree's rsx_vk_present_wait.h (patch 2042;
# tools/ps3recomp-patched.sh first). No GPU, no Vulkan headers.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/rsx_vk_present_wait.h" ]] || { echo "FAIL: no rsx_vk_present_wait.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=c11 -O0 -Wall -Wextra -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_present_wait.c" -o "$out/t"
timeout 60 "$out/t"
