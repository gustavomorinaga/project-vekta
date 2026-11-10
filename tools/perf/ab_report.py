#!/usr/bin/env python3
"""Report of a tools/perf/ab.sh run: one line per run and the paired A/B difference.

usage: ab_report.py <out dir>

Per run (from the game's own lines, port/kz1hd/route.cpp and perf.cpp): fps over the route, 1 % and 0.1 % low, p50/p99,
the main PPU thread's CPU time per frame (its CPU share over the replay window, read by perf.cpp from
/proc/self/task/<tid>/stat at the first frame after the load and at the end, times the window's length over its
presents), loadavg, and the busiest other processes (top, an instant 3 s reading), and whether the route stayed on its
recorded path (position differences).

Pairs are the runs in order (A then B, or B then A, as ab.sh ran them); the difference B - A of each pair cancels the
slow drift of a session. The interval is mean +- t(0.975, n-1) * sd / sqrt(n) of the paired differences (Student's t),
an approximation that assumes the pair differences are independent and roughly normal."""
import glob, math, os, re, statistics, sys

T975 = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23, 12: 2.18,
        15: 2.13, 20: 2.09, 30: 2.04}
def t975(df):
    if df <= 0: return float("nan")
    k = max(x for x in T975 if x <= df)
    return T975[k] if df <= 30 else 1.96

def parse(log):
    s = open(log, errors="replace").read()
    r = {}
    m = re.search(r"^\[route\] replay ended: (\d+) frames in ([\d.]+) s = ([\d.]+) fps, 1% low [\d.]+ fps, (\d+) position "
                  r"differences \(at most ([\d.e+-]+)\)", s, re.M)
    if not m: return None
    r["frames"], r["secs"], r["fps"], r["posdiff"], r["posmax"] = int(m[1]), float(m[2]), float(m[3]), int(m[4]), float(m[5])
    m = re.search(r"^\[perf\] replay: (\d+) presents in ([\d.]+) s = [\d.]+ fps; ms p50 ([\d.]+) p90 [\d.]+ p99 ([\d.]+) "
                  r"p99\.9 [\d.]+ max ([\d.]+); 1% low ([\d.]+) fps, 0\.1% low ([\d.]+) fps", s, re.M)
    if m:
        r["presents"], r["psecs"], r["p50"], r["p99"], r["max"], r["low1"], r["low01"] = \
            int(m[1]), float(m[2]), float(m[3]), float(m[4]), float(m[5]), float(m[6]), float(m[7])
    m = re.search(r"^\[perf\] replay: thread CPU: (.*)$", s, re.M)
    pid = None
    pf = log[:-4] + ".pid"
    if os.path.exists(pf): pid = open(pf).read().split()[-1]
    if m and pid and "presents" in r:
        for item in m[1].split(", "):
            name_tid, pct = item.rsplit(" ", 1)
            if name_tid.rsplit("/", 1)[1] == pid:   # the main PPU thread is the process's first thread
                r["main_pct"] = float(pct.rstrip("%"))
                r["main_ms"] = r["main_pct"] / 100.0 * r["psecs"] * 1000.0 / r["presents"]
    # whole-process and per-group CPU per frame, from <run>.cpu (route_play.sh: every thread's CPU ticks once a
    # second): the CPU used between the first and the last sample, per second, times the replay's seconds per frame
    cpu = log[:-4] + ".cpu"
    if os.path.exists(cpu) and pid and r.get("frames") and r.get("secs"):
        snaps, cur = [], None
        for l in open(cpu):
            f = l.split(None, 2)
            if f[0] == "t": cur = (float(f[1]), {}); snaps.append(cur)
            elif cur and len(f) >= 2: cur[1][f[0]] = (int(f[1]), f[2].strip() if len(f) > 2 else "")
        if len(snaps) >= 3:
            # from the second sample (the first may precede the window) to the last one that still has nearly all
            # threads (the very last can catch the process exiting, with only the main thread left)
            most = max(len(x[1]) for x in snaps)
            last = max(i for i, x in enumerate(snaps) if len(x[1]) >= 0.9 * most)
            (t0, a), (t1, b) = snaps[1], snaps[last]
            hz = os.sysconf("SC_CLK_TCK"); ms_per_frame = r["secs"] * 1000.0 / r["frames"]
            groups = {"main": 0.0, "rsx": 0.0, "spu": 0.0, "rest": 0.0}
            for tid, (ticks, nm) in b.items():
                d = (ticks - a.get(tid, (ticks, nm))[0]) / hz / (t1 - t0)   # cores used
                g = "main" if tid == pid else "rsx" if nm.startswith("frame") else "spu" if nm.startswith(("spurs", "spu")) else "rest"
                groups[g] += d
            r["cpu_groups"] = {g: v * ms_per_frame for g, v in groups.items()}
            r["cpu_total"] = sum(r["cpu_groups"].values())
    env = log[:-4] + ".env"
    r["load"], r["top"] = "", []
    if os.path.exists(env):
        for l in open(env):
            if l.startswith("loadavg_mid"): r["load"] = l.split(None, 1)[1].strip()
            elif l.startswith("loadavg_start") and not r["load"]: r["load"] = l.split(None, 1)[1].strip()
            elif l.startswith("top "): r["top"].append(" ".join(l.split()[2:3]) + " " + l.split()[1] + "%")
    return r

d = sys.argv[1]
setup = open(os.path.join(d, "setup.txt")).read() if os.path.exists(os.path.join(d, "setup.txt")) else ""
runs = []
for log in sorted(glob.glob(os.path.join(d, "[0-9][0-9]-[AB].log"))):
    tag = os.path.basename(log)[:-4]
    r = parse(log)
    runs.append((tag, r))
print(setup.rstrip())
print("(CPU ms/frame below: whole process and main / RSX / SPURS+SPU / rest, from route_play.sh's per-second /proc samples)")
print(f"{'run':6} {'fps':>7} {'1%low':>6} {'0.1%':>6} {'p50ms':>6} {'p99ms':>6} {'mainCPU':>7} {'ms/fr':>6} {'frames':>6} {'posdiff':>8}  load(1/5/15)  others")
for tag, r in runs:
    if not r: print(f"{tag:6} (no route summary: the run did not finish)"); continue
    print(f"{tag:6} {r['fps']:7.1f} {r.get('low1', float('nan')):6.1f} {r.get('low01', float('nan')):6.1f} "
          f"{r.get('p50', float('nan')):6.2f} {r.get('p99', float('nan')):6.2f} {r.get('main_pct', float('nan')):6.1f}% "
          f"{r.get('main_ms', float('nan')):6.3f} {r['frames']:6d} {r['posdiff']:8d}  {r['load']:14} {', '.join(r['top'])}")
    if "cpu_groups" in r:
        g = r["cpu_groups"]
        print(f"{'':6} all threads {r['cpu_total']:6.2f} ms/frame = main {g['main']:5.2f} + rsx {g['rsx']:5.2f} + spu {g['spu']:5.2f} + rest {g['rest']:5.2f}")
ok = [(t, r) for t, r in runs if r]
if len({(r["frames"], r["posdiff"]) for _, r in ok}) > 1:
    print("WARNING: the runs did not all replay the same frames (frames/position differences differ): not the same scene")
pairs = []
for i in range(0, len(ok) - 1, 2):
    (t1, r1), (t2, r2) = ok[i], ok[i + 1]
    if t1[-1] == t2[-1]: continue
    a, b = (r1, r2) if t1.endswith("A") else (r2, r1)
    pairs.append((a, b))
print(f"\n{len(pairs)} pairs; paired difference B - A (mean, 95 % interval):")
for key, label, unit in (("fps", "fps", ""), ("low1", "1% low fps", ""), ("low01", "0.1% low fps", ""),
                         ("p50", "p50 frame ms", ""), ("main_ms", "main thread CPU ms/frame", ""),
                         ("cpu_total", "all threads CPU ms/frame", "")):
    diffs = [b[key] - a[key] for a, b in pairs if key in a and key in b]
    if not diffs: continue
    am = statistics.mean(a[key] for a, b in pairs if key in a and key in b)
    m = statistics.mean(diffs)
    if len(diffs) > 1:
        h = t975(len(diffs) - 1) * statistics.stdev(diffs) / math.sqrt(len(diffs))
        ci = f"[{m - h:+.3f}, {m + h:+.3f}]"
    else: ci = "(one pair: no interval)"
    print(f"  {label:26} A mean {am:9.3f}   B - A {m:+8.3f} ({100 * m / am:+.2f} %)  {ci}")
