#!/usr/bin/env bash
# Patches 2084 and 2085: builds and runs test_vk_breadcrumbs.c against the patched tree (tools/ps3recomp-patched.sh
# first): the render breadcrumb ring and its lines (rsx_vk_breadcrumbs.h) and the GPU wait in steps
# (rsx_vk_gpu_wait.h), with a made-up waiter in place of the GPU.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
for h in rsx_vk_breadcrumbs.h rsx_vk_gpu_wait.h; do
  [[ -f "$P/libs/video/$h" ]] || { echo "FAIL: no $h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
done
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_vk_breadcrumbs.c" -o "$out/t"
timeout 60 "$out/t"
