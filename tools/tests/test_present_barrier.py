#!/usr/bin/env python3
"""Host test for patch 2041: when the window has bars, the present clears the swapchain image and then blits the
frame onto it. Both are transfer writes to the same pixels, so a pipeline barrier must sit between them, or a driver
may run them out of order and parts of the clear land over the frame (port issue #17: black blocks at 16:10 on AMD).

Reads vk_present_window in the patched tree (tools/ps3recomp-patched.sh first) and checks the command order: the
clear of sc_images, then a vkCmdPipelineBarrier, then the vkCmdBlitImage. No GPU needed."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P = os.environ.get("PS3RECOMP_DIR") or os.path.join(ROOT, "work", "ps3recomp-patched")
src_path = os.path.join(P, "libs", "video", "rsx_vulkan_backend.c")
if not os.path.isfile(src_path):
    print(f"FAIL: no patched tree at {P}; run tools/ps3recomp-patched.sh", file=sys.stderr)
    sys.exit(1)
src = open(src_path, encoding="utf-8").read()
m = re.search(r"^static void vk_present_window\(void\)\n\{\n(.*?)^\}\n", src, re.S | re.M)
if not m:
    print("FAIL: vk_present_window not found", file=sys.stderr)
    sys.exit(1)
body = m.group(1)
calls = [(c.start(), c.group(1)) for c in re.finditer(r"\bpvk(Cmd\w+)\(", body)]
clear = [i for i, (_, n) in enumerate(calls) if n == "CmdClearColorImage"]
blit = [i for i, (_, n) in enumerate(calls) if n == "CmdBlitImage"]
fails = []
if len(clear) != 1 or len(blit) != 1:
    fails.append(f"expected one clear and one blit in vk_present_window, found {len(clear)} and {len(blit)}")
elif clear[0] > blit[0]:
    fails.append("the clear comes after the blit")
else:
    between = [n for _, n in calls[clear[0] + 1:blit[0]]]
    if "CmdPipelineBarrier" not in between:
        fails.append("no vkCmdPipelineBarrier between the bars' clear and the frame's blit (write-after-write)")
    else:
        seg = body[calls[clear[0]][0]:calls[blit[0]][0]]
        if not re.search(r"dstAccessMask\s*=\s*VK_ACCESS_TRANSFER_WRITE_BIT", seg):
            fails.append("the barrier between the clear and the blit does not make the blit's transfer write wait")
for f in fails:
    print("FAIL:", f)
print("test_present_barrier:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
