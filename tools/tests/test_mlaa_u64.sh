#!/usr/bin/env bash
# The MLAA shader without 64-bit integers (patch 2087, #129), on the patched tree (tools/ps3recomp-patched.sh first;
# no GPU, no glslang). Checks: the (low, high) word form of the SPU float emulation agrees with the uint64_t form on
# random inputs (test_mlaa_u64.c); the shader's SPIR-V declares no Int64 capability; the backend writes the compile
# marker around the MLAA compile and no longer checks for AMD's driver.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/vulkan/rsx_vk_mlaa_comp.spv.h" ]] || { echo "FAIL: no MLAA shader in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O2 -UNDEBUG -Wall -Werror -Wno-unused-function "$ROOT/tools/tests/test_mlaa_u64.c" -o "$out/t"
timeout 120 "$out/t" 3000000
python3 - "$P/libs/video" <<'PY'
import os, re, sys
video = sys.argv[1]
fails = []
words = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]{8})', open(os.path.join(video, 'vulkan', 'rsx_vk_mlaa_comp.spv.h')).read())]
if not words or words[0] != 0x07230203: sys.exit('FAIL: rsx_vk_mlaa_comp.spv.h is not SPIR-V')
caps, i = set(), 5
while i < len(words):
    n, op = words[i] >> 16, words[i] & 0xFFFF
    if op == 17: caps.add(words[i + 1])                                    # OpCapability
    if n == 0: break
    i += n
if 11 in caps: fails.append('the MLAA shader still declares the Int64 capability')
if 'int64' in open(os.path.join(video, 'vulkan', 'rsx_vk_mlaa.comp')).read().replace('uint64_t form', ''):
    fails.append('rsx_vk_mlaa.comp still uses 64-bit integers')
src = open(os.path.join(video, 'rsx_vulkan_backend.c')).read()
if 'vk_mlaa_marker_set(1);\n        const VkResult r = vk_create_post_pipeline(&pci, &s_mlaa.pipe, "MLAA");\n        vk_mlaa_marker_set(0);' not in src:
    fails.append('the MLAA compile is not inside the marker')
if 'driver_id != 1u' in src: fails.append('the backend still leaves the MLAA out on AMD by driver id')
for f in fails: print('FAIL:', f)
print('test_mlaa_u64 (shader, backend):', 'FAIL' if fails else 'ok')
sys.exit(1 if fails else 0)
PY
