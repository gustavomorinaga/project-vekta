#!/usr/bin/env python3
"""Host test for tools/sync-from-private.py's patch handling (no private repository needed): the public author line,
no co-author trailers, the area block a patch is numbered in, and the file name it gets."""
import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("sync", os.path.join(ROOT, "tools", "sync-from-private.py"))
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def patch(paths, subject="x: y", body="Why it changed.\n\nCo-Authored-By: An Agent <noreply@example.com>\n"):
    head = f"From 0123 Mon Sep 17 00:00:00 2001\nFrom: Someone Private <me@example.org>\nDate: today\nSubject: [PATCH 3/9] {subject}\n\n{body}---\n"
    return head + "".join(f"diff --git a/{p} b/{p}\n--- a/{p}\n+++ b/{p}\n@@ -1 +1 @@\n-a\n+b\n" for p in paths)


t = sync.clean(patch(["libs/spurs/cellSpurs.c"]))
check(sync.PUBLIC_AUTHOR + "\n" in t, "the author line is the public one")
check("Subject: [PATCH] x: y" in t, "the [PATCH n/m] tag is reduced to [PATCH]")
check("Co-Authored-By" not in t, "the co-author trailer is dropped")
check("Why it changed." in t, "the message body is kept")
check(sync.body_of(t) == sync.body_of(patch(["libs/spurs/cellSpurs.c"])), "clean() leaves the diff alone")

for paths, block in [
    (["libs/spurs/cellSpurs.c"], 1), (["runtime/spu/spu_dma.c"], 1),
    (["libs/video/vulkan/rsx_vk.c"], 2), (["libs/gcm/cellGcm.c"], 2),
    (["tools/ppu_lifter/emit.py"], 3), (["libs/audio/cellAudio.c"], 3),
    (["libs/input/cellPad.c"], 4), (["libs/trophy/sceNpTrophy.c"], 5),
    (["CMakeLists.txt"], 0),
    (["libs/video/a.c", "libs/video/b.c", "libs/input/c.c"], 2),   # the area most of the files are in
]:
    check(sync.area(patch(paths)) == block, f"{paths} is in block {block}xxx, not {sync.area(patch(paths))}xxx")

s = sync.slug("cellPad: key capture for a title's remapping screen (KZ_KEYS=1), and more words than fit")
check(len(s) <= 52 and not s.endswith("-"), f"slug is cut to 52 characters without a trailing dash: {s}")
check(sync.slug("a  b//c") == "a-b-c", "slug joins every run of other characters with one dash")

sync.CTX["P"] = "/private"
check(sync.rewrite(b'see docs/wws-jm-post.md, "O MLAA em C"') == b'see docs/dev/wws-jm-post.md, "The MLAA in C"',
      "private doc names and Portuguese headings are rewritten")
check(sync.rewrite(b"\xff\xfe binary") == b"\xff\xfe binary", "binary data is left alone")

print("test_sync_from_private:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
