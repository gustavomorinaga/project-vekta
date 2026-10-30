#!/usr/bin/env python3
"""check_harness.py <cap>...: every captured job rerun alone (its PUTs against the game's), then
every full chain of the capture rerun on the frame the game gave it, against what the game left."""
import sys, subprocess, mlaa, capframes
bad = 0
for cap in sys.argv[1:]:
    info = subprocess.run([mlaa.C + '/hb/harness', mlaa.C + '/' + cap, 'info'], capture_output=True, text=True).stdout
    n = len(info.splitlines())
    for j in range(n - 1):   # the last job is cut by the end of the capture
        r = subprocess.run([mlaa.C + '/hb/harness', mlaa.C + '/' + cap, 'replay', str(j)], capture_output=True, text=True)
        if r.returncode: bad += 1; print(r.stdout.strip(), r.stderr.strip())
    print(f'{cap}: {n - 1} jobs replayed, {bad} differ')
    for c0, base, before, after in capframes.chains(mlaa.C + '/' + cap):
        h = mlaa.run(before, cap=cap)
        d = int((h[:720] != after[:720]).any(2).sum())
        bad += d > 0
        print(f'{cap}: chain from job {c0} (image {base:08X}): {d} pixels differ from the game')
sys.exit(1 if bad else 0)
