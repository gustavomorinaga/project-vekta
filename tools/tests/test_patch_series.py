#!/usr/bin/env python3
"""Host test for port/patches: every patch is named NNNN-<slug>.patch with a number used once and in an area block
(0xxx to 5xxx), every patch is in series exactly once and every series line is a patch, and no patch header carries a
co-author trailer. (tools/ps3recomp-patched.sh then checks that the series applies.)"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P = os.path.join(ROOT, "port", "patches")
fails = []

files = sorted(f for f in os.listdir(P) if f != "series")
series = [l.strip() for l in open(os.path.join(P, "series"), encoding="utf-8") if l.strip() and not l.startswith("#")]

for f in files:
    if not re.fullmatch(r"[0-5]\d{3}-[A-Za-z0-9_.-]+\.patch", f):
        fails.append(f"{f}: not NNNN-<slug>.patch with NNNN in 0000-5999")
numbers = [f[:4] for f in files]
for n in sorted({n for n in numbers if numbers.count(n) > 1}):
    fails.append(f"number {n} is used by more than one patch")
for f in sorted(set(series) - set(files)):
    fails.append(f"series names {f}, which is not a file")
for f in sorted(set(files) - set(series)):
    fails.append(f"{f} is not in series")
for f in sorted({f for f in series if series.count(f) > 1}):
    fails.append(f"{f} is in series more than once")
for f in files:
    head = open(os.path.join(P, f), encoding="utf-8", errors="replace").read().split("\ndiff --git ", 1)[0]
    if re.search(r"^Co-Authored-By:", head, re.M | re.I):
        fails.append(f"{f}: co-author trailer in the header")
    if not re.search(r"^Subject: ", head, re.M):
        fails.append(f"{f}: no Subject line (the header tools/sync-from-private.py matches patches by)")

for f in fails:
    print("FAIL:", f)
print(f"test_patch_series: {len(files)} patches,", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
