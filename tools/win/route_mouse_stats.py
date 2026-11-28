"""How evenly a recorded route's frames got the mouse's counts (diagnostic, not for merge).

    python route_mouse_stats.py <route file> [...]

A route (KZ_ROUTE_REC) has an "F n now prev ms r r2" line per frame and an "M dx dy" line for each player tick the
mouse moved. Over the stretch from the first to the last M line (a steady sweep from tools/win/mouse_sweep.py), it
prints the frames, the share of them that got no counts at all, and the spread of counts per millisecond of frame
time. A host read on every frame gives counts on (nearly) every frame and a small spread; a host read at 64 Hz with
the game at 144 fps leaves about half the frames without counts and the rest with lumps.
"""
import statistics
import sys


def frames(path):
    out = []   # [ms, |dx| summed over the frame's ticks]
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "F" and len(p) >= 5:
                out.append([float(p[4]), 0])
            elif p[0] == "M" and len(p) >= 3 and out:
                out[-1][1] += abs(int(p[1]))
    return out


def report(path):
    fr = frames(path)
    moved = [i for i, (_, dx) in enumerate(fr) if dx]
    if len(moved) < 10:
        print(f"{path}: fewer than 10 frames with mouse counts")
        return
    a, b = moved[0], moved[-1]
    rates, zero = [], 0
    for i in range(a, b):
        dt = fr[i + 1][0] - fr[i][0]
        if dt <= 0:
            continue
        if not fr[i][1]:
            zero += 1
        rates.append(fr[i][1] / dt)
    n = len(rates)
    mean = statistics.fmean(rates)
    cv = statistics.pstdev(rates) / mean if mean else 0.0
    dts = [fr[i + 1][0] - fr[i][0] for i in range(a, b)]
    print(f"{path}: {n} frames over {fr[b][0] - fr[a][0]:.0f} ms (median frame {statistics.median(dts):.2f} ms); "
          f"{100.0 * zero / n:.1f} % got no counts; counts/ms mean {mean:.2f}, spread (CV) {cv:.2f}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        report(p)
