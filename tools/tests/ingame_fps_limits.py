#!/usr/bin/env python3
"""In-game check of the frame-rate limits (pc_rates.h: 30, 60, 120, 144, 165, 240, Unlimited). Needs the game, a GPU
and a display; not a host test. Each limit: a fresh profile in a test VFS, V-Sync off, into Level 1 (PAD_SCRIPT of
docs/dev/testing.md), then [kz-stats] over the last part of the run (KZ_STATS: flips and the game clock's ticks
every 2 s) and [perf] windows (KZ_PERF_EVERY=10: 1 % low, the main thread's CPU). It checks that the frame rate stays at or under the limit, that the game clock runs at its rate in real
time (ticks per second / clock Hz within 3 %: the game is neither fast nor slow), and that the run ends alive.

    tools/tests/ingame_fps_limits.py --vfs work/vfs-test --out work/fps-limits [--limits 60,120,0]

The VFS must be a test copy: its kz1hd_pc.ini is rewritten and its savedata and cache are deleted for every run.
Every run waits for the machine's benchmark lock (one game at a time) and runs on CPUs 0-3.
"""
import argparse
import fcntl
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LEVEL1 = "15:0x0008,25:0x4000,35:0x0008,45:0x4000,60:0x4000,66:0x4000," + ",".join(
    f"{t}:0x4000" for t in range(72, 121, 6))   # PAD_SCRIPT into Level 1 (docs/dev/testing.md)
BASE = {"display": "window", "resolution": "1280x720", "refresh": "0", "vsync": "on", "fps_limit": "60",
        "scale": "100", "overlay": "off", "widescreen": "on", "aspect": "auto", "intro": "off",
        "upscaler": "linear", "temporal": "off", "reflex": "off", "antialiasing": "mlaa", "language": "system"}


def write_ini(vfs, settings):
    with open(os.path.join(vfs, "kz1hd_pc.ini"), "w") as f:
        for k, v in sorted(settings.items()):
            f.write(f"{k}={v}\n")

TICK = {0: 1000}   # pc_tick_hz: the limit itself, 1000 for Unlimited (0)


def run(limit, args):
    out = os.path.join(args.out, f"limit-{limit}")
    os.makedirs(out, exist_ok=True)
    vfs = os.path.abspath(args.vfs)
    subprocess.run(["rm", "-rf", os.path.join(vfs, "gamedata/dev_hdd0/home/00000001/savedata/NPUA80856"),
                    os.path.join(vfs, "cache")], check=True)
    write_ini(vfs, dict(BASE, fps_limit=str(limit), vsync="off"))
    log = os.path.join(out, "run.log")
    env = dict(os.environ, PS3RECOMP_VK_WINDOW="1", SDL_VIDEODRIVER="x11", KZ_VFS=vfs, KZ_STATS="1",
               PAD_SCRIPT=LEVEL1, PS3RECOMP_ESC_QUITS="0", KZ_PERF_EVERY="10")
    lock = open(args.lock, "a")
    fcntl.flock(lock, fcntl.LOCK_EX)
    subprocess.run(["taskset", "-c", "0-3", "env", f"KZ_BIN={os.path.abspath(args.bin)}",
                    os.path.join(ROOT, "tools/run.sh"), str(args.secs), log], env=env)
    lock.close()
    with open(log, errors="replace") as f:
        lines = f.read().splitlines()
    stats = [(int(m.group(1)), int(m.group(2)), int(m.group(3))) for m in
             (re.search(r"\[kz-stats\] t=(\d+)s flips=(\d+) .* ticks=(\d+)", ln) for ln in lines) if m]
    hz = TICK.get(limit, limit if limit >= 100 else 30 if limit <= 30 else 60)
    play = stats[-int(args.window / 2):]   # the last part: Level 1 in play
    if len(play) < 4:
        return limit, "FAIL", f"{len(stats)} stats lines: the game ended early"
    dt = play[-1][0] - play[0][0]
    fps = (play[-1][1] - play[0][1]) / dt if dt else 0.0
    speed = (play[-1][2] - play[0][2]) / dt / hz if dt else 0.0
    # the [perf] windows inside the measured part: 1 % low and the main thread (the busiest KZ1HD-* thread)
    perf, low, cpu = [], {}, {}
    for ln in lines:
        m = re.search(r"\[perf\] window (\d+): \d+ presents .* 1% low ([\d.]+) fps", ln)
        if m:
            perf.append(int(m.group(1))); low[int(m.group(1))] = float(m.group(2))
        m = re.search(r"\[perf\] window (\d+): thread CPU: (.*)", ln)
        if m:
            cpu[int(m.group(1))] = max([float(x) for x in re.findall(r"KZ1HD[^/]*/\d+ ([\d.]+)%", m.group(2))] or [0.0])
    last = perf[-max(1, int(args.window / 10)):]
    low1 = min(low[w] for w in last) if last else 0.0
    main_ms = (sum(cpu.get(w, 0.0) for w in last) / len(last) / 100.0 * 1000.0 / fps) if last and fps else 0.0
    warns = sorted({re.sub(r"0x[0-9A-Fa-f]+|\d+", "N", ln)[:100] for ln in lines
                    if re.match(r"\[kz\]", ln) and re.search(r"warn|fail|error|lost|stall|timeout", ln, re.I)})
    bad = [p for p in ("device lost", "[kz-crash]", "Segmentation fault") if p in "\n".join(lines)]
    ok = not bad and abs(speed - 1.0) <= 0.03 and (limit == 0 or fps <= limit * 1.03) and fps > 20
    return limit, "PASS" if ok else "FAIL", (f"{fps:.1f} fps over {dt} s, game clock {speed:.3f} x real time "
                                             f"at {hz} Hz; 1% low {low1:.1f} fps; main thread {main_ms:.2f} ms/frame" +
                                             (f"; {', '.join(bad)}" if bad else "") +
                                             (f"; [kz] warnings: {' | '.join(warns)}" if warns else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vfs", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bin", default=os.path.join(ROOT, "work/build/KZ1HD"))
    ap.add_argument("--limits", default="30,60,120,144,165,240,0")
    ap.add_argument("--secs", type=int, default=200, help="each run's length; Level 1 is in play from about 140 s")
    ap.add_argument("--window", type=int, default=40, help="seconds of play measured, at the end")
    ap.add_argument("--lock", default=os.environ.get("BENCH_LOCK", os.path.join(ROOT, "work/bench.lock")))
    args = ap.parse_args()
    args.out = os.path.abspath(args.out)
    if os.path.realpath(args.vfs) == os.path.realpath(os.path.join(ROOT, "work/vfs")):
        sys.exit("--vfs is the player's run folder: use a test copy")
    res = []
    for lim in args.limits.split(","):
        r = run(int(lim), args)
        res.append(r)
        print(f"{r[1]}  limit {r[0] or 'Unlimited':>9}  {r[2]}", flush=True)
    sys.exit(0 if all(r[1] == "PASS" for r in res) else 1)


if __name__ == "__main__":
    main()
