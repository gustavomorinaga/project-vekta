#!/usr/bin/env python3
"""check_mb.py <cap> [seed]: the C reference (port/kz1hd/native/mb_ref.c, through mb_ref_cli) against the lifted
SPU code (harness) on random inputs, stage by stage, for the first frame of the capture that has every stage.
Prints, per stage, how many pixels differ (inside the image, and the whole of it)."""
import os, subprocess, sys, tempfile
import numpy as np

HB = os.environ.get('MB_HB', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'work', 'mb_cap', 'hb'))
cap = sys.argv[1]; rng = np.random.default_rng(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
tmp = tempfile.mkdtemp(prefix='mbchk')
IO = 0x11300000                       # EA of IO offset 0 (v0.18)
FIELD, H556, H637 = IO + 0xF1000, IO + 0x556000, IO + 0x637000
HS, FS = 640 * 360 * 4, 1280 * 720 * 4

info = subprocess.run([f'{HB}/harness', cap, 'info'], capture_output=True, text=True, check=True).stdout.split('\n')
mods = [int(l.split()[3].rstrip(':')) for l in info if l.startswith('job')]
runs = []                              # (module, first job, count)
for j, m in enumerate(mods):
    if runs and runs[-1][0] == m and runs[-1][1] + runs[-1][2] == j: runs[-1][2] += 1
    else: runs.append([m, j, 1])

def first(pattern):
    """first index in runs where the modules (and counts) follow pattern [(module, count), ...]"""
    for i in range(len(runs) - len(pattern) + 1):
        if all(runs[i + k][0] == m and runs[i + k][2] == n for k, (m, n) in enumerate(pattern)): return i
    sys.exit(f'pattern {pattern} not in the capture')

def harness(j0, j1, loads, dumps):
    a = [f'{HB}/harness', cap, 'run', str(j0), str(j1)]
    for ea, arr in loads:
        p = f'{tmp}/l{ea:X}.bin'; arr.astype(np.uint8).tofile(p); a.append(f'{ea:X}={p}')
    for ea, n in dumps: a.append(f'@{ea:X}+{n:X}={tmp}/d{ea:X}.bin')
    subprocess.run(a, check=True, stderr=subprocess.DEVNULL)
    return [np.fromfile(f'{tmp}/d{ea:X}.bin', np.uint8) for ea, n in dumps]

def ref(*args):
    subprocess.run([f'{HB}/mb_ref_cli', *args], check=True)

def report(name, got, want, shape):
    g, w = got.reshape(shape), want.reshape(shape)
    bad = (g != w).any(-1)
    print(f'{name:8} {bad.sum():7} of {bad.size} pixels differ' + (f', max {np.abs(g.astype(int) - w).max()}' if bad.any() else ''))
    return bad

rnd = lambda *s: rng.integers(0, 256, s, dtype=np.uint8)

# the field: Modulate (24 jobs: the RSX field; the other 24 are the glow's) and the four blurs
i = first([(202, 48), (203, 2), (206, 5), (203, 2), (206, 5), (204, 24), (201, 24), (204, 24), (201, 24)])
f = rnd(360, 640, 4)
# the field jobs, then the blurs (the MLAA jobs in between are skipped)
fj = runs[i][1]; bj = runs[i + 5][1]
(g,) = harness(fj, fj + 23, [(FIELD, f)], [(H637, HS)])
f.tofile(f'{tmp}/f.bin'); ref('field', f'{tmp}/f.bin', f'{tmp}/f.out')
# Modulate alone is a copy: check it, then the blurs on its output
report('modulate', g, f, (360, 640, 4))
(g,) = harness(bj, bj + 95, [(H637, f)], [(H637, HS)])
report('field', g, np.fromfile(f'{tmp}/f.out', np.uint8), (360, 640, 4))

# Downsample, MotionBlur, Upsample+blend of one image
i = first([(209, 108), (207, 48), (200, 64), (209, 96)])
dj, gj, uj = runs[i + 1][1], runs[i + 2][1], runs[i + 3][1]
dlog = subprocess.run([f'{HB}/harness', cap, 'recs', str(dj)], capture_output=True, text=True).stdout.split('\n')
full_ea = min(int(l.split()[1], 16) for l in dlog if l.startswith('G') and int(l.split()[3], 16) == 0x140) & ~0xFFFF
full_ea = IO + 0x1D2000 if abs(full_ea - (IO + 0x1D2000)) < 0x400000 else IO + 0x7F9000
full = rnd(768, 1280, 4)
(g,) = harness(dj, dj + 47, [(full_ea, full)], [(H556, HS)])
full[:720].tofile(f'{tmp}/full.bin'); ref('down', f'{tmp}/full.bin', f'{tmp}/half.out')
report('down', g, np.fromfile(f'{tmp}/half.out', np.uint8), (360, 640, 4))

half = rnd(360 + 16, 640, 4)          # with 8 rows above and below (what memory holds there must not matter)
fld = rnd(360 + 8, 640, 4)
(g,) = harness(gj, gj + 63, [(H556 - 8 * 2560, half), (H637, fld)], [(H637, HS)])
half[8:368].tofile(f'{tmp}/h.bin'); fld[:360].tofile(f'{tmp}/g.bin')
ref('gather', f'{tmp}/h.bin', f'{tmp}/g.bin', f'{tmp}/g.out')
bad = report('gather', g, np.fromfile(f'{tmp}/g.out', np.uint8), (360, 640, 4))
if bad.any():
    ys, xs = np.nonzero(bad)
    print('         by row band', np.bincount(ys // 30, minlength=12).tolist(), 'by col band', np.bincount(xs // 40, minlength=16).tolist())
    print('         first rows', np.bincount(ys, minlength=360)[:10].tolist(), 'last rows', np.bincount(ys, minlength=360)[-10:].tolist())

res = rnd(360 + 32, 640, 4); full = rnd(768, 1280, 4)
(g,) = harness(uj, uj + 95, [(full_ea, full), (H637 - 16 * 2560, res)], [(full_ea, FS)])
full[:720].tofile(f'{tmp}/full.bin'); res[16:376].tofile(f'{tmp}/r.bin')
ref('blend', f'{tmp}/full.bin', f'{tmp}/r.bin', f'{tmp}/b.out')
report('blend', g, np.fromfile(f'{tmp}/b.out', np.uint8), (720, 1280, 4))
