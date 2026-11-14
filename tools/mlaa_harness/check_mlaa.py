#!/usr/bin/env python3
"""check_mlaa.py: the MLAA checks, one "ok"/"FAIL" line each (run by regress_mlaa.sh).

- the lifted jobs replayed against the game (check_harness.py on the captures);
- the C reference (mlaa_ref_cli) against the lifted code: every pixel, on
  images with an 8-pixel border of one colour (no runs reach the SPU's edge defect),
  and on the captured game frames, where only the edge deviation may remain
  (reported: pixels and largest channel difference);
- the GPU pass (mlaa_vk_test) against the C reference: every pixel, on all of them;
- the same at the sizes the port draws the display buffer at under its internal
  resolution scale (mlaa_ref_wh, patch 2080): random rectangles and the game frames
  upscaled (nearest) to 2560x1440, 3440x1440, 1707x960 and 1281x721.
MLAA_WORK: the work directory (captures, hb/ with harness, mlaa_ref_cli,
mlaa_vk_test, mlaa.spv)."""
import os, sys, subprocess, numpy as np
import mlaa, capframes

C = mlaa.C
fail = 0
def report(name, ok, extra=''):
    global fail
    print(('ok   ' if ok else 'FAIL ') + name + (('  (' + extra + ')') if extra else ''))
    fail |= not ok

def run_c(img):
    img[:720].tofile(mlaa.T + '/c.argb')
    subprocess.run([C + '/hb/mlaa_ref_cli', mlaa.T + '/c.argb', mlaa.T + '/co.argb'], check=True)
    return np.fromfile(mlaa.T + '/co.argb', np.uint8).reshape(720, 1280, 4)
def run_gpu(img):
    img[:720].tofile(mlaa.T + '/g.argb')
    subprocess.run([C + '/hb/mlaa_vk_test', C + '/hb/mlaa.spv', mlaa.T + '/g.argb', mlaa.T + '/go.argb'],
                   check=True, capture_output=True)
    return np.fromfile(mlaa.T + '/go.argb', np.uint8).reshape(720, 1280, 4)
def run_c_wh(img, w, h):
    img.tofile(mlaa.T + '/c.argb')
    subprocess.run([C + '/hb/mlaa_ref_cli', mlaa.T + '/c.argb', mlaa.T + '/co.argb', str(w), str(h)], check=True)
    return np.fromfile(mlaa.T + '/co.argb', np.uint8).reshape(h, w, 4)
def run_gpu_wh(img, w, h):
    img.tofile(mlaa.T + '/g.argb')
    subprocess.run([C + '/hb/mlaa_vk_test', C + '/hb/mlaa.spv', mlaa.T + '/g.argb', mlaa.T + '/go.argb', '1', str(w), str(h)],
                   check=True, capture_output=True)
    return np.fromfile(mlaa.T + '/go.argb', np.uint8).reshape(h, w, 4)
def rects(seed, n=1500, flat_border=False, W=1280, H=720):
    rng = np.random.default_rng(seed)
    I = np.zeros((H, W, 4), np.uint8)
    I[:, :, 1:] = rng.integers(0, 256, 3)
    for _ in range(n):
        x, y = rng.integers(-40, W), rng.integers(-30, H)
        w, h = rng.integers(2, 50), rng.integers(1, 30)
        I[max(y, 0):max(y + h, 0), max(x, 0):max(x + w, 0), 1:] = rng.integers(0, 256, 3)
    if flat_border:   # one colour around: no run reaches the 4 first pixels of a line or its end
        c = rng.integers(0, 256, 3)
        I[:8, :, 1:] = c; I[-8:, :, 1:] = c; I[:, :8, 1:] = c; I[:, -8:, 1:] = c
    return I

r = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'check_harness.py'), 'cap', 'cap_real'],
                   capture_output=True, text=True, cwd=C)
report('lifted jobs and chains replay the game', r.returncode == 0)

images = []
for s in range(6):
    I = rects(1000 + s, flat_border=True)
    h, c = mlaa.run(I)[:720], run_c(I)
    d = int((h != c).any(2).sum())
    report('C = lifted code, test image %d (flat border)' % s, d == 0, '%d pixels differ' % d)
    images.append(I)
for cap in ('cap', 'cap_real'):
    for c0, base, before, after in capframes.chains(C + '/' + cap):
        I = before[:720].copy()
        c = run_c(I)
        d = (c[:, :, 1:] != after[:720, :, 1:]).any(2)
        changed = int((after[:720, :, 1:] != I[:, :, 1:]).any(2).sum())
        mx = int(np.abs(c.astype(int) - after[:720].astype(int)).max())
        report('C = game, %s frame %d (all but the SPU edge defect)' % (cap, c0), d.sum() < 0.01 * changed,
               '%d of %d changed pixels differ, at most %d' % (d.sum(), changed, mx))
        images.append(I)
for k in range(8):
    p = C + '/gdump/%04d_in.argb' % k
    if os.path.exists(p): images.append(np.fromfile(p, np.uint8).reshape(720, 1280, 4))
images += [rects(2000 + s, n=4000) for s in range(4)]
bad = 0
for I in images:
    bad += int((run_gpu(I) != run_c(I)).any(2).sum())
report('GPU = C on %d images' % len(images), bad == 0, '%d pixels differ' % bad)
for (w, h) in ((2560, 1440), (3440, 1440), (1707, 960), (1281, 721)):
    sized = [rects(3000 + w + s, n=1500 * w * h // 921600, W=w, H=h) for s in range(2)]
    sized += [np.ascontiguousarray(I[(np.arange(h) * 720 // h)][:, (np.arange(w) * 1280 // w)]) for I in images[6:9]]
    bad = sum(int((run_gpu_wh(I, w, h) != run_c_wh(I, w, h)).any(2).sum()) for I in sized)
    report('GPU = C at %dx%d on %d images' % (w, h, len(sized)), bad == 0, '%d pixels differ' % bad)
sys.exit(1 if fail else 0)
