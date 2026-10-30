#!/usr/bin/env python3
"""Source lines behind the KZ_PROF samples of the functions matching a regex.

Usage: prof_lines.py <kz_prof.txt> <binary> <function regex> [--top N]
Needs the code built with -g. Each sample is charged to its innermost inlined
location, so a helper's own lines show up under the helper's file.
"""
import bisect, collections, os, re, subprocess, sys

prof, binary, pat = sys.argv[1:4]
top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 30
rx = re.compile(pat)

syms = []
for line in subprocess.run(["nm", "--defined-only", binary], capture_output=True, text=True).stdout.splitlines():
    p = line.split(" ", 2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
syms.sort()
addrs = [a for a, _ in syms]
exe = [l.split() for l in open(prof + ".maps") if l.split()[5:6] and "x" in l.split()[1]
       and os.path.basename(l.split()[5]) == os.path.basename(binary).split(".prof")[0]][0]
bias = int(exe[0].split("-")[0], 16) - int(exe[2], 16)

hits = collections.Counter()
for l in open(prof):
    f = [x for x in l.split() if x[:1] != "t"]
    if len(f) < 3: continue   # tid name pc [stack...]: the sampled pc is the third
    try: a = int(f[2], 16) - bias
    except ValueError: continue
    i = bisect.bisect_right(addrs, a) - 1
    if i >= 0 and rx.search(syms[i][1]):
        hits[a] += 1
out = subprocess.run(["addr2line", "-e", binary, "-a"] + [hex(a) for a in hits],
                     capture_output=True, text=True).stdout.split("\n")
loc, cur = {}, None
for l in out:
    if l.startswith("0x"): cur = int(l, 16)
    elif l and cur is not None: loc[cur] = l.split(" ")[0]; cur = None
lines = collections.Counter()
for a, n in hits.items():
    lines[loc.get(a, "?")] += n
total = sum(lines.values())
cache = {}
def text(where):
    path, _, ln = where.rpartition(":")
    if not ln.isdigit(): return ""
    if path not in cache:
        try: cache[path] = open(path, errors="replace").read().split("\n")
        except OSError: cache[path] = []
    src = cache[path]
    return src[int(ln) - 1].strip()[:90] if 0 < int(ln) <= len(src) else ""
print(f"{total} samples in functions matching {pat}")
for k, v in lines.most_common(top):
    print(f"{v:6d} {100.0 * v / total:5.1f}%  {os.path.basename(k):28s} {text(k)}")
