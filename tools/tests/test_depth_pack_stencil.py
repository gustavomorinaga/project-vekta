#!/usr/bin/env python3
"""Patch 2076: a depth target read as A8R8G8B8 carries its stencil byte. Host test on the patched tree
(tools/ps3recomp-patched.sh first; no GPU, no glslang): the depth pack's SPIR-V (rsx_vk_depth_pack_comp.spv.h, what
the renderer runs) is parsed for its push-constant block, which must have the third word, the stencil's word offset
in the buffer (Killzone HD's glow source multiplies its glow texture by that byte; without it the byte was 0 and the
glow black), and the backend must push three words, declare a 12-byte range, copy the stencil aspect after the depth
floats and size the buffer for it. Before the patch the block had two words and these checks fail."""
import os, re, subprocess, sys

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
env = subprocess.run(['bash', '-c', f'KZ_ROOT="{root}"; . "{root}/tools/env.sh"; printf %s "$PS3RECOMP_DIR"'],
                     capture_output=True, text=True, check=True).stdout
video = os.path.join(env, 'libs', 'video')
hdr = os.path.join(video, 'vulkan', 'rsx_vk_depth_pack_comp.spv.h')
backend = os.path.join(video, 'rsx_vulkan_backend.c')
fails = []
def check(ok, msg):
    if not ok: fails.append(msg)

if not os.path.isfile(hdr):
    sys.exit(f'FAIL: no {hdr}; run tools/ps3recomp-patched.sh')
words = [int(w, 16) for w in re.findall(r'0x([0-9a-fA-F]{8})', open(hdr).read())]
check(words and words[0] == 0x07230203, 'the header is not SPIR-V')

# SPIR-V: OpDecorate (71) id Block (2); OpMemberDecorate (72) id member Offset (35) n;
# OpTypeStruct (30) id members...; OpTypePointer (32) id PushConstant (9) type
blocks, offsets, structs, pc_types = set(), {}, {}, set()
i = 5
while i < len(words):
    n, op = words[i] >> 16, words[i] & 0xFFFF
    a = words[i + 1:i + n]
    if op == 71 and len(a) >= 2 and a[1] == 2: blocks.add(a[0])
    if op == 72 and len(a) >= 4 and a[2] == 35: offsets.setdefault(a[0], {})[a[1]] = a[3]
    if op == 30: structs[a[0]] = a[1:]
    if op == 32 and len(a) >= 3 and a[1] == 9: pc_types.add(a[2])
    if n == 0: break
    i += n
pc = [t for t in pc_types if t in blocks and t in structs]
check(len(pc) == 1, f'one push-constant block expected, found {len(pc)}')
if pc:
    members = structs[pc[0]]
    offs = sorted(offsets.get(pc[0], {}).values())
    check(len(members) == 3, f'the push-constant block has {len(members)} words, not 3 (w, h, stencil_at)')
    check(offs == [0, 4, 8], f'push-constant offsets {offs}, not [0, 4, 8]')

src = open(backend).read()
a = src.find('static int vk_pack_init(void)'); b = src.find('static int vk_create_device_buffer', a)
check(a >= 0 and 'VkPushConstantRange pr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 12 };' in src[a:b],
      'vk_pack_init does not declare a 12-byte push-constant range')
check('VK_IMAGE_ASPECT_STENCIL_BIT' in src[src.find('static u32 vk_pack_copy('):src.find('static u32 vk_eng_depth_packed(')],
      'vk_pack_copy does not copy the stencil aspect')
check(src.count('const u32 pc[3] = {') >= 2 and 'stencil_at };' in src,
      'the pack callers do not push the stencil offset')
check(src.count('vk_pack_size(') >= 3, 'the pack buffers are not sized for the stencil bytes (vk_pack_size)')

if fails:
    for f in fails: print('FAIL:', f)
    sys.exit(1)
print('test_depth_pack_stencil: ok')
