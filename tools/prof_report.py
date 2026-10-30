#!/usr/bin/env python3
"""Per-thread, per-function table of a KZ_PROF sample file (see port/kz1hd/prof.cpp).

Usage: prof_report.py <kz_prof.txt> <binary> [--top N] [--group]
  --group  also fold lifted SPU functions into their job module / image
           (jobmod_<hash>, pm_<addr>) and PPU functions into "PPU code".
The binary's load bias is taken from the first sample that lands in main.
"""
import bisect, collections, subprocess, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 25
if "--top" in sys.argv: args.remove(str(top))
if "--callers" in sys.argv: args.remove(sys.argv[sys.argv.index("--callers") + 1])
group = "--group" in sys.argv
samples_path, binary = args[0], args[1]

syms = []
for line in subprocess.run(["nm", "-C", "--defined-only", binary], capture_output=True, text=True).stdout.splitlines():
    parts = line.split(" ", 2)
    if len(parts) == 3 and parts[1] in "tTwW":
        syms.append((int(parts[0], 16), parts[2]))
syms.sort()
addrs = [a for a, _ in syms]

# PIE: find the bias from /proc maps is not available offline, so take it from
# the executable's own text range: the most common high bits of the samples.
samples = [[x for x in l.split() if x[:1] != "t"] for l in open(samples_path)]   # KZ_PROF_TS adds t<ns>
rips = [int(s[2], 16) for s in samples if len(s) >= 3]
import os
maps = []   # (start, end, offset, path) of executable mappings, from <samples>.maps
if os.path.exists(samples_path + ".maps"):
    for l in open(samples_path + ".maps"):
        f = l.split()
        if len(f) >= 6 and "x" in f[1]:
            a, b = (int(x, 16) for x in f[0].split("-"))
            maps.append((a, b, int(f[2], 16), f[5]))
lo = addrs[0]
bias_counts = collections.Counter((r - lo) & ~0xFFFFF for r in rips if r > lo)
bias = 0
pie = subprocess.run(["readelf", "-h", binary], capture_output=True, text=True).stdout
exe = [m for m in maps if os.path.basename(m[3]) == os.path.basename(binary).split(".prof")[0]]
if exe:
    bias = exe[0][0] - exe[0][2]   # file offset == vaddr for this binary's text
elif "DYN" in pie and bias_counts:
    # choose the bias that maps the most samples into known symbols
    best = 0
    for b, _ in bias_counts.most_common(8):
        b = b & ~0xFFF
        hit = sum(1 for r in rips[:5000] if addrs[0] <= r - b <= addrs[-1] + 0x10000)
        if hit > best: best, bias = hit, b

def name(rip):
    for m in maps:
        if m[0] <= rip < m[1] and os.path.basename(m[3]) != os.path.basename(binary).split(".prof")[0]:
            return os.path.basename(m[3]) or "[anon]"
    a = rip - bias
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0 or a - addrs[i] > 0x200000: return "?(%x)" % rip
    n = syms[i][1].split("(")[0]
    if group:
        if n.startswith("jobmod_") or n.startswith("pm_"):
            n = n.split("_spu_func")[0]
        elif n.startswith("func_"):
            n = "PPU code"
    return n

def raw(rip):
    i = bisect.bisect_right(addrs, rip - bias) - 1
    return syms[i][1] if i >= 0 else ""

def in_exe(rip):
    return not any(m[0] <= rip < m[1] and os.path.basename(m[3]) != os.path.basename(binary).split(".prof")[0] for m in maps)

# --callers SYM (needs KZ_PROF_STACK=1): who calls SYM, by the first other
# function of the binary on the sample's stack.
if "--callers" in sys.argv:
    want = sys.argv[sys.argv.index("--callers") + 1]
    cc = collections.Counter()
    for s in samples:
        if len(s) <= 3 or name(int(s[2], 16)) != want: continue
        for r in (int(x, 16) for x in s[3:]):
            n = name(r) if in_exe(r) else ""
            if n and n != want and "on_prof" not in raw(r) and "prof" not in n:
                cc[n] += 1; break
    tot = sum(cc.values())
    print(f"callers of {want}: {tot} samples")
    for k, v in cc.most_common(top): print(f"  {v:7d} {100.0 * v / max(tot, 1):5.1f}%  {k}")
    sys.exit(0)

# --incl (needs KZ_PROF_STACK=1): inclusive time, each function counted once
# per sample it is anywhere on the stack of -- which subtrees the time is in.
if "--incl" in sys.argv:
    cc, tot = collections.Counter(), 0
    for s in samples:
        if len(s) <= 3: continue
        tot += 1
        seen = set()
        for r in [int(s[2], 16)] + [int(x, 16) for x in s[3:]]:
            n = name(r) if in_exe(r) else ""
            if n and n not in seen and "prof" not in n: seen.add(n); cc[n] += 1
    print(f"inclusive, {tot} samples with a stack")
    for k, v in cc.most_common(top): print(f"  {v:7d} {100.0 * v / max(tot, 1):5.1f}%  {k}")
    sys.exit(0)

# KZ_PROF_WALL samples carry the call stack after the rip: charge a sample that
# landed in a library (a futex wait, a memcpy) to the first caller in the binary.
for i, s in enumerate(samples):
    if len(s) > 3:
        rip = int(s[2], 16)
        if not in_exe(rip):
            ex = [r for r in (int(x, 16) for x in s[3:]) if in_exe(r) and "on_prof" not in raw(r)]
            if ex: rip = ex[0]
        samples[i] = s[:2] + ["%x" % rip]

per = collections.defaultdict(collections.Counter)
names = {}
for s in samples:
    if len(s) != 3: continue
    tid, tname, rip = s[0], s[1], int(s[2], 16)
    names[tid] = tname
    per[tid][name(rip)] += 1
total = sum(sum(c.values()) for c in per.values())
print(f"{total} samples (1 per ms of thread CPU time), bias 0x{bias:x}")
for tid in sorted(per, key=lambda t: -sum(per[t].values())):
    c = per[tid]; n = sum(c.values())
    if n < total * 0.01: continue
    print(f"\n== tid {tid} {names[tid]}: {n} ms")
    for k, v in c.most_common(top):
        print(f"  {v:7d} {100.0 * v / n:5.1f}%  {k}")

# --ops: lifted SPU samples by the SPU operation on their source line (needs the
# SPU sources built with -g). Answers "which SPU instructions cost the most".
if "--ops" in sys.argv:
    import re
    lifted = collections.Counter()
    for s in samples:
        if len(s) != 3: continue
        rip = int(s[2], 16)
        n = name(rip)
        if n.startswith("jobmod_") or n.startswith("pm_") or n.startswith("spu_0"):
            lifted[rip - bias] += 1
    addrs_l = list(lifted)
    out = subprocess.run(["addr2line", "-e", binary, "-i", "-a"] + [hex(a) for a in addrs_l],
                         capture_output=True, text=True).stdout.split("\n")
    loc = {}; cur = None
    for l in out:
        if l.startswith("0x"): cur = int(l, 16); loc[cur] = []
        elif l and cur is not None: loc[cur].append(l)
    srcs = {}
    def src_line(path, ln):
        if path not in srcs:
            try: srcs[path] = open(path).read().split("\n")
            except OSError: srcs[path] = []
        return srcs[path][ln - 1] if 0 < ln <= len(srcs[path]) else ""
    ops = collections.Counter()
    for a, n in lifted.items():
        ls = [l for l in loc.get(a, []) if "spu_recomp.c" in l]
        if not ls: ops["(no line)"] += n; continue
        path, _, ln = ls[-1].split(" ")[0].rpartition(":")
        text = src_line(path, int(ln) if ln.isdigit() else 0)
        m = re.search(r"(spu_[a-z0-9_]+)\(", text)
        key = m.group(1) if m else ("goto/if" if ("goto" in text or "if (" in text) else text.strip()[:40])
        ops[key] += n
    t = sum(ops.values())
    print(f"\n== lifted SPU samples by operation ({t} ms)")
    for k, v in ops.most_common(top):
        print(f"  {v:7d} {100.0 * v / t:5.1f}%  {k}")
