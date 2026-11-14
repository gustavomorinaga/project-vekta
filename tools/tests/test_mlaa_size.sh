#!/usr/bin/env bash
# The GPU MLAA at any drawn size of the display buffer (patch 2080), on the patched tree (tools/ps3recomp-patched.sh
# first; no GPU, no glslang). Before the patch the pass ran only on a 1280x720 surface: under the internal
# resolution scale (the default scale=100 with fill draws the frame at the window's size) the MLAA, the glow and the
# sprint motion blur did not run at all. Checks: rsx_mlaa_size.h (test_mlaa_size.c: the sizes the pass takes, the
# title's 5x256 / 5x144 jobs and dispatches at 1280x720, buffers and dispatches that cover a bigger size); the
# shader's SPIR-V push-constant block carries the size (6 words, not 2) and its scratch bindings 7 and 8; the
# backend asks rsx_mlaa_size_ok instead of 1280x720.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/rsx_mlaa_size.h" ]] || { echo "FAIL: no rsx_mlaa_size.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_mlaa_size.c" -o "$out/t"
timeout 60 "$out/t"
python3 - "$P/libs/video" <<'PY'
import os, re, sys
video = sys.argv[1]
fails = []
words = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]{8})', open(os.path.join(video, 'vulkan', 'rsx_vk_mlaa_comp.spv.h')).read())]
if not words or words[0] != 0x07230203: sys.exit('FAIL: rsx_vk_mlaa_comp.spv.h is not SPIR-V')
blocks, structs, pc_types, bindings = set(), {}, set(), set()
i = 5
while i < len(words):
    n, op = words[i] >> 16, words[i] & 0xFFFF
    a = words[i + 1:i + n]
    if op == 71 and len(a) >= 2 and a[1] == 2: blocks.add(a[0])            # OpDecorate Block
    if op == 71 and len(a) >= 3 and a[1] == 33: bindings.add(a[2])         # OpDecorate Binding n
    if op == 30: structs[a[0]] = a[1:]                                     # OpTypeStruct
    if op == 32 and len(a) >= 3 and a[1] == 9: pc_types.add(a[2])          # OpTypePointer PushConstant
    if n == 0: break
    i += n
pc = [t for t in pc_types if t in blocks and t in structs]
if len(pc) != 1: fails.append(f'one push-constant block expected, found {len(pc)}')
elif len(structs[pc[0]]) != 6: fails.append(f'push constants: {len(structs[pc[0]])} words, want 6 (stage, job, w, h, cj, rj)')
for b in (7, 8):
    if b not in bindings: fails.append(f'no binding {b} (the per-workgroup scratch) in the shader')
src = open(os.path.join(video, 'rsx_vulkan_backend.c')).read()
m = re.search(r'static int vk_eng_mlaa\(void\* user, u32 surface, u32 field\)\n\{(.*?)\n\}\n', src, re.S)
if not m: fails.append('vk_eng_mlaa not found')
else:
    body = m.group(1)
    if '1280' in body or '720' in body: fails.append('vk_eng_mlaa still names 1280 or 720')
    if 'rsx_mlaa_size_ok(o->lw, o->lh, o->w, o->h)' not in body: fails.append('vk_eng_mlaa does not ask rsx_mlaa_size_ok')
    if 'vk_glow_record_out' not in body or 'vk_mb_record_out' not in body:
        fails.append('the glow and the motion blur do not run over the scaled frame')
if 'VkPushConstantRange pr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 24 };' not in src: fails.append('the MLAA layout does not declare 24 bytes of push constants')
for f in fails: print('FAIL:', f)
print('test_mlaa_size (shader, backend):', 'FAIL' if fails else 'ok')
sys.exit(1 if fails else 0)
PY
