#!/usr/bin/env python3
"""check_glow.py <cap> [seed]: the glow's C reference (port/kz1hd/native/glow_ref.c, through glow_ref_cli) against
the lifted SPU code (harness), for the first frame of the capture that has the whole glow chain: the stages one by one
on random inputs, then the chain from the half-resolution copy to the full image on random, sparse (bright dots on
black with alpha 255, the game's source) and black inputs. Prints how many pixels differ per case."""
import os, subprocess, sys, tempfile
import numpy as np

HB = os.environ.get('MB_HB', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'work', 'mb_cap', 'hb'))
cap = sys.argv[1]; rng = np.random.default_rng(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
tmp = tempfile.mkdtemp(prefix='glowchk')
IO = 0x11300000                       # EA of IO offset 0 (v0.18)
SRC, H718, Q, Q2 = IO + 0x10000, IO + 0x718000, IO + 0xB7D000, IO + 0xBB5400
HS, QS, FS = 640 * 360 * 4, 320 * 180 * 4, 1280 * 720 * 4

info = subprocess.run([f'{HB}/harness', cap, 'info'], capture_output=True, text=True, check=True).stdout.split('\n')
mods = [int(l.split()[3].rstrip(':')) for l in info if l.startswith('job')]
runs = []                              # [module, first job, count]
for j, m in enumerate(mods):
    if runs and runs[-1][0] == m and runs[-1][1] + runs[-1][2] == j: runs[-1][2] += 1
    else: runs.append([m, j, 1])
# Modulate x48 (field and glow source), MLAA, the field's blurs, then the glow: Downsample x12, 7 blur pairs,
# Modulate x6, 4 blur pairs, Upsample x108 (12 doubling the quarter image, 96 adding to the full image)
pattern = [(202, 48), (203, 2), (206, 5), (203, 2), (206, 5), (204, 24), (201, 24), (204, 24), (201, 24), (207, 12)] + \
          [(204, 6), (201, 6)] * 7 + [(202, 6)] + [(204, 6), (201, 6)] * 4 + [(209, 108)]
for i in range(len(runs) - len(pattern) + 1):
    if all(runs[i + k][0] == m and runs[i + k][2] == n for k, (m, n) in enumerate(pattern)): break
else: sys.exit('the glow chain is not in the capture')
mod0 = runs[i][1] + 24                 # the glow source's Modulate (factor 1.0): the second 24 of the 48
down0 = runs[i + 9][1]; quarter0 = runs[i + 10][1]; up0 = runs[i + len(pattern) - 1][1]
F1, F2 = IO + 0x1D2000, IO + 0x7F9000    # the two full images; the frame adds to one of them

def harness(j0, j1, loads, dumps):
    a = [f'{HB}/harness', cap, 'run', str(j0), str(j1)]
    for ea, arr in loads:
        p = f'{tmp}/l{ea:X}.bin'; arr.astype(np.uint8).tofile(p); a.append(f'{ea:X}={p}')
    for ea, n in dumps: a.append(f'@{ea:X}+{n:X}={tmp}/d{ea:X}.bin')
    subprocess.run(a, check=True, stderr=subprocess.DEVNULL)
    return [np.fromfile(f'{tmp}/d{ea:X}.bin', np.uint8) for ea, n in dumps]

def ref(*args):
    subprocess.run([f'{HB}/glow_ref_cli', *[str(a) for a in args]], check=True)

def cfile(name, arr): p = f'{tmp}/{name}.bin'; arr.astype(np.uint8).tofile(p); return p

fail = 0
def report(name, got, want, shape):
    global fail
    g, w = got.reshape(shape), want.reshape(shape)
    bad = (g != w).any(-1)
    fail |= bool(bad.any())
    print(f'{name:16} {bad.sum():7} of {bad.size} pixels differ' + (f', max {np.abs(g.astype(int) - w).max()}' if bad.any() else ''))

rnd = lambda *s: rng.integers(0, 256, s, dtype=np.uint8)

# Modulate 1.0 of the source: a copy. Its last job (rows 300-359, columns 480-639) is left out: the harness cannot
# rerun it (no lifted function at the LS address it jumps to).
x = rnd(360, 640, 4)
(g,) = harness(mod0, mod0 + 22, [(SRC, x)], [(H718, HS)])
keep = np.ones((360, 640), bool); keep[300:, 480:] = False
report('modulate 1.0', g.reshape(360, 640, 4)[keep], x[keep], (-1, 4))

def chain(half, full):
    """the SPU chain from the half-resolution copy (IO 0x718000) to the full image it adds to"""
    g1, g2 = harness(down0, up0 + 107, [(H718, np.vstack([half, np.zeros((8, 640, 4), np.uint8)])), (F1, full), (F2, full)],
                     [(F1, FS), (F2, FS)])
    return g2 if (g1 == full[:720].ravel()).all() else g1

def c_chain(half, full, strength=256):
    ref('all', cfile('full', full[:720]), cfile('src', half), f'{tmp}/c.bin', strength)
    return np.fromfile(f'{tmp}/c.bin', np.uint8)

cases = {'random': (rnd(360, 640, 4), rnd(768, 1280, 4))}
dots = np.zeros((360, 640, 4), np.uint8); dots[..., 0] = 255
for _ in range(60):
    y, x0 = rng.integers(0, 360), rng.integers(0, 640)
    dots[max(0, y - 2):y + 3, max(0, x0 - 2):x0 + 3, 1:] = rng.integers(64, 256, 3)
dots[0:4, :, 1:] = 200; dots[:, -3:, 2] = 255           # the edges
cases['dots'] = (dots, rnd(768, 1280, 4))
black = np.zeros((360, 640, 4), np.uint8); black[..., 0] = 255
cases['black source'] = (black, rnd(768, 1280, 4))
for name, (half, full) in cases.items():
    report(name, chain(half, full), c_chain(half, full), (720, 1280, 4))
    if name == 'black source':
        f = full[:720].copy(); f[..., 0] = 255             # alpha saturates, the colour stays
        report('black: image kept', c_chain(half, full), f, (720, 1280, 4))
        report('strength 0', c_chain(dots, full, 0), full[:720], (720, 1280, 4))
print('PASS' if not fail else 'FAIL')
sys.exit(fail)
