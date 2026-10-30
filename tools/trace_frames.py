#!/usr/bin/env python3
"""Per-frame breakdown of a PS3_TRACE file (runtime/ps3_trace.h).

usage: trace_frames.py <trace.json> [--frame N]
Frames run flip to flip. For each thread it sums its spans by name (and by
argument for usleep, the guest call site; for spurs_wid, the workload), then
averages over the frames. --frame prints one frame's spans in order.
"""
import collections, json, sys

ev = json.load(open(sys.argv[1]))["traceEvents"]
flips = []
for t in sorted(e["ts"] for e in ev if e["name"] == "flip"):
    if not flips or t - flips[-1] > 2000:   # the port presents a flip from two places
        flips.append(t)
if len(flips) < 3:
    sys.exit("fewer than 3 flips in the trace")
frames = list(zip(flips, flips[1:]))
dur = [b - a for a, b in frames]
print(f"{len(frames)} frames, {sum(dur) / len(dur) / 1000:.1f} ms average ({1e6 * len(dur) / sum(dur):.1f} fps)")
def key(e):
    return e["name"] + (" " + e["args"]["a"] if e["name"] in ("usleep", "spurs_wid") else "")
per = collections.defaultdict(lambda: collections.Counter())
for e in ev:
    if e["name"] == "flip":
        continue
    s, t = e["ts"], e["ts"] + e["dur"]
    if t <= frames[0][0] or s >= frames[-1][1]:
        continue
    per[e["tid"]][key(e)] += min(t, frames[-1][1]) - max(s, frames[0][0])
n = len(frames)
for tid, c in sorted(per.items(), key=lambda kv: -sum(kv[1].values())):
    tot = sum(c.values())
    print(f"\ntid {tid}: {tot / n / 1000:.1f} ms/frame traced")
    for k, v in c.most_common(8):
        print(f"   {v / n / 1000:7.2f} ms  {k}")
if "--frame" in sys.argv:
    i = int(sys.argv[sys.argv.index("--frame") + 1])
    a, b = frames[i]
    print(f"\nframe {i}: {(b - a) / 1000:.1f} ms")
    for e in sorted((e for e in ev if a <= e["ts"] < b and e["name"] != "flip"), key=lambda e: e["ts"]):
        if e["dur"] >= 200:
            print(f"  {(e['ts'] - a) / 1000:7.2f} +{e['dur'] / 1000:6.2f} ms  tid {e['tid']}  {key(e)}")
