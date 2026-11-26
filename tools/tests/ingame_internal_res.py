#!/usr/bin/env python3
"""In-game check of the internal resolution while running (ps3recomp patch 2093). Needs the game, a GPU and a display;
not a host test. Each run starts the game with a window from a fresh profile in a test VFS, plays into Level 1
(PAD_SCRIPT of docs/dev/testing.md), then KZ_TEST_DISPLAY_SEQ changes the display mode, the resolution and the
internal resolution there, as the PC page does. After each step it checks:

  * the backend's own line: the last "frame rendered at WxH" before the next step is the size rsx_fit_scale gives for
    the window's swapchain then (fill or fit, a forced aspect's box, the scale);
  * the picture: frames dumped a few seconds after the step (PS3RECOMP_VK_DUMP_TRIGGER) are that size and not black;
  * under a temporal upscaler, a "scene drawn at" line for that output, smaller than it;
  * that the game is still running at the end: no crash, no lost device; with --validation, no validation error.

Every run takes the benchmark lock (one game at a time on the machine) and runs on CPUs 0-3.

    tools/tests/ingame_internal_res.py --vfs work/vfs-test --out work/internal-res [--runs A,B] [--validation]

The VFS must be a test copy: its kz1hd_pc.ini is rewritten and its savedata and cache are deleted for every run.
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LEVEL1 = "15:0x0008,25:0x4000,35:0x0008,45:0x4000,60:0x4000,66:0x4000," + ",".join(
    f"{t}:0x4000" for t in range(72, 121, 6))
FIRST = 150     # seconds: the first step, Level 1 in play
GAP = 12        # seconds between steps
BASE = {"display": "window", "resolution": "1280x720", "refresh": "0", "vsync": "on", "fps_limit": "60",
        "scale": "100", "overlay": "off", "widescreen": "on", "aspect": "auto", "intro": "off",
        "upscaler": "linear", "temporal": "off", "reflex": "off", "antialiasing": "mlaa", "language": "system"}

# name: (start settings, steps (mode, WxH, scale %), "-" keeps it)
RUNS = {
    "A": ({}, [("window", "3440x1440", "-"), ("-", "-", "50"), ("-", "-", "200"), ("window", "1280x720", "100"),
               ("borderless", "-", "-"), ("fullscreen", "1920x1080", "-"), ("fullscreen", "2560x1440", "50"),
               ("window", "1280x720", "100")]),
    "B": ({"display": "fullscreen", "resolution": "3440x1440", "scale": "200", "upscaler": "fsr"},
          [("fullscreen", "1280x720", "-"), ("-", "-", "100"), ("borderless", "-", "-"), ("window", "1920x1080", "-"),
           ("window", "2560x1440", "50"), ("fullscreen", "3440x1440", "100")]),
    "C": ({"resolution": "1920x1080", "widescreen": "off"},
          [("window", "2560x1440", "-"), ("-", "-", "200"), ("fullscreen", "3440x1440", "-"), ("-", "-", "50"),
           ("borderless", "-", "100")]),
    "D": ({"aspect": "16:9"},
          [("window", "3440x1440", "-"), ("-", "-", "200"), ("borderless", "-", "50"), ("window", "1280x720", "100")]),
    "E": ({"temporal": "fsr3-quality"},
          [("window", "3440x1440", "-"), ("window", "1280x720", "-"), ("window", "3440x1440", "50"), ("-", "-", "100")]),
    "F": ({"temporal": "dlss-quality"},
          [("window", "3440x1440", "-"), ("window", "1280x720", "-"), ("window", "3440x1440", "50"), ("-", "-", "100")]),
}


def fit(w, h, k, fill, aspect, gw=1280, gh=720, max_dim=16384):
    """rsx_fit_scale and rsx_scale_dim (rsx_fit_scale.h), in float32 as far as it matters here."""
    if aspect:
        if w > h * aspect:
            w = int(h * aspect + 0.5)
        else:
            h = int(w / aspect + 0.5)
    sx, sy = w / gw * k, h / gh * k
    if not fill:
        sx = sy = min(sx, sy)
    while gw * sx > max_dim or gh * sy > max_dim:
        sx *= 0.9
        sy *= 0.9
    return max(1, int(gw * sx + 0.5)), max(1, int(gh * sy + 0.5))


def ppm_size_and_mean(path):
    with open(path, "rb") as f:
        data = f.read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not m:
        return None, 0.0
    px = data[m.end():]
    sample = px[::997]
    return (int(m.group(1)), int(m.group(2))), (sum(sample) / len(sample) if sample else 0.0)


def write_ini(vfs, settings):
    with open(os.path.join(vfs, "kz1hd_pc.ini"), "w") as f:
        for k, v in sorted(settings.items()):
            f.write(f"{k}={v}\n")


def run_one(name, args):
    start, steps = RUNS[name]
    settings = dict(BASE, **start)
    out = os.path.join(args.out, name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    vfs = os.path.abspath(args.vfs)
    subprocess.run(["rm", "-rf", os.path.join(vfs, "gamedata/dev_hdd0/home/00000001/savedata/NPUA80856"),
                    os.path.join(vfs, "cache")], check=True)
    write_ini(vfs, settings)
    seq = ",".join(f"{FIRST + i * GAP}:{m}:{r}:{s}" for i, (m, r, s) in enumerate(steps))
    trigger = os.path.join(out, "dump.go")
    env = dict(os.environ, PS3RECOMP_VK_WINDOW="1", SDL_VIDEODRIVER="x11", KZ_VFS=vfs, KZ_STATS="1",
               PAD_SCRIPT=LEVEL1, KZ_TEST_DISPLAY_SEQ=seq, PS3RECOMP_VK_DUMP=os.path.join(out, "f%06u.ppm"),
               PS3RECOMP_VK_DUMP_TRIGGER=trigger, PS3RECOMP_ESC_QUITS="0")
    if args.validation:
        env["PS3RECOMP_VK_VALIDATION"] = "1"
    secs = FIRST + len(steps) * GAP + 6
    log = os.path.join(out, "run.log")
    binary = os.path.abspath(args.bin)
    cmd = ["flock", args.lock, "taskset", "-c", "0-3", "env", f"KZ_BIN={binary}",
           os.path.join(ROOT, "tools/run.sh"), str(secs + 5), log]
    t0 = time.monotonic()
    proc = subprocess.Popen(cmd, env=env, start_new_session=True)
    # a few dumped frames 8 s into each step (the game's clock starts with its first frame, a few seconds in)
    for i in range(len(steps)):
        at = FIRST + i * GAP + 8 + args.lead
        while time.monotonic() - t0 < at and proc.poll() is None:
            time.sleep(0.05)
        if proc.poll() is not None:
            break
        open(trigger, "w").close()
        time.sleep(0.08)
        os.remove(trigger)
    try:
        proc.wait(timeout=secs + 120)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
    return check(name, settings, steps, out, log, args)


def check(name, settings, steps, out, log, args):
    with open(log, errors="replace") as f:
        lines = f.read().splitlines()
    k0 = int(settings["scale"]) / 100
    fill = settings["widescreen"] != "off"
    aspect = 16 / 9 if settings["aspect"] == "16:9" else 0.0
    temporal = settings["temporal"] != "off"
    # where each step's line is, then the lines up to the next one
    marks = [i for i, ln in enumerate(lines) if "[kz-test] display step" in ln]
    results = []
    k = k0
    # the dumps, named by present count, in bursts (one a step): split where the count jumps
    dumps = sorted(f for f in os.listdir(out) if f.endswith(".ppm"))
    groups, last = [], None
    for d in dumps:
        tag = int(re.sub(r"\D", "", d))
        size, mean = ppm_size_and_mean(os.path.join(out, d))
        if last is None or tag - last > 30:
            groups.append([])
        groups[-1].append((size, mean))
        last = tag
        if not args.keep_dumps:
            os.remove(os.path.join(out, d))
    for n, (mode, res, sc) in enumerate(steps):
        if sc != "-":
            k = int(sc) / 100
        label = f"{name}{n + 1} {mode}/{res}/{sc}"
        if n >= len(marks):
            results.append((label, "FAIL", "step never ran (the game ended or did not reach it)"))
            continue
        end = marks[n + 1] if n + 1 < len(marks) else len(lines)
        seg = lines[:end]
        sc_line = [m for m in (re.search(r"window swapchain (\d+)x(\d+)", ln) for ln in seg) if m]
        fr = [m for m in (re.search(r"frame rendered at (\d+)x(\d+)", ln) for ln in seg) if m]
        if not sc_line or not fr:
            results.append((label, "FAIL", "no swapchain or frame line"))
            continue
        sw, sh = int(sc_line[-1].group(1)), int(sc_line[-1].group(2))
        want = fit(sw, sh, k, fill, aspect)
        got = (int(fr[-1].group(1)), int(fr[-1].group(2)))
        notes = [f"swapchain {sw}x{sh}", f"frame {got[0]}x{got[1]}"]
        ok = got == want
        if not ok:
            notes.append(f"expected {want[0]}x{want[1]}")
        if temporal:
            scene = [m for m in (re.search(r"scene drawn at (\d+)x(\d+) for (\d+)x(\d+)", ln) for ln in seg) if m]
            if not scene or (int(scene[-1].group(3)), int(scene[-1].group(4))) != want:
                ok = False
                notes.append("no temporal scene line for that output")
            else:
                notes.append(f"scene {scene[-1].group(1)}x{scene[-1].group(2)}")
        if mode != "borderless" and res != "-" and (sw, sh) != tuple(map(int, res.split("x"))):
            notes.append(f"(asked {res})")
        # the frames dumped for this step: the n-th group of dumps, in order
        grp = groups[n] if n < len(groups) and len(groups) == len(steps) else []
        seen = {s for s, _ in grp if s}
        if not grp:
            ok = False
            notes.append(f"no dump ({len(groups)} bursts for {len(steps)} steps)")
        elif seen != {want}:
            ok = False
            notes.append(f"dumps {sorted(seen)}")
        elif max(m for _, m in grp) < 4.0:
            ok = False
            notes.append("dumps black")
        else:
            notes.append(f"dump {want[0]}x{want[1]} mean {max(m for _, m in grp):.0f}")
        results.append((label, "PASS" if ok else "FAIL", ", ".join(notes)))
    text = "\n".join(lines)
    bad = [p for p in ("device lost", "[kz-crash]", "Segmentation fault", "rescale: the") if p in text]
    if args.validation:
        nerr = len(re.findall(r"validation error", text))
        results.append((f"{name} validation", "PASS" if nerr == 0 else "FAIL", f"{nerr} validation error line(s)"))
    alive = len(marks) == len(steps) and not bad
    results.append((f"{name} run", "PASS" if alive else "FAIL",
                    f"{len(marks)}/{len(steps)} steps" + (f"; {', '.join(bad)}" if bad else "")))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vfs", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bin", default=os.path.join(ROOT, "work/build/KZ1HD"))
    ap.add_argument("--runs", default="A,B,C,D,E")   # F: DLSS, in a build with the DLSS SDK
    ap.add_argument("--validation", action="store_true")
    ap.add_argument("--lock", default=os.environ.get("BENCH_LOCK", os.path.join(ROOT, "work/bench.lock")))
    ap.add_argument("--lead", type=float, default=3.0, help="seconds from the process start to the game's first frame")
    ap.add_argument("--keep-dumps", action="store_true")
    args = ap.parse_args()
    args.out = os.path.abspath(args.out)   # the game runs from the VFS: the dump paths must not be relative
    if os.path.realpath(args.vfs) == os.path.realpath(os.path.join(ROOT, "work/vfs")):
        sys.exit("--vfs is the player's run folder: use a test copy")
    os.makedirs(args.out, exist_ok=True)
    allres = []
    for name in args.runs.split(","):
        res = run_one(name, args)
        allres += res
        for r in res:
            print(f"{r[1]}  {r[0]:34} {r[2]}", flush=True)
    with open(os.path.join(args.out, "results.tsv"), "w") as f:
        for r in allres:
            f.write("\t".join(r) + "\n")
    sys.exit(0 if all(r[1] == "PASS" for r in allres) else 1)


if __name__ == "__main__":
    main()
