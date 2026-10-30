#!/usr/bin/env python3
"""Contact sheet of numbered PPM frame dumps (PS3RECOMP_VK_DUMP=dir/f%06u.ppm).

Usage: ppm_sheet.py OUT.png FRAME.ppm... [--cols 4] [--scale 4]
Each frame is shrunk by --scale (box filter) and labelled by its file order.
"""
import argparse, struct, zlib

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("frames", nargs="+")
ap.add_argument("--cols", type=int, default=4)
ap.add_argument("--scale", type=int, default=4)
a = ap.parse_args()

def load(path):
    d = open(path, "rb").read()
    magic, dims, _, px = d.split(b"\n", 3)
    w, h = map(int, dims.split())
    return w, h, px.ljust(w * h * 3, b"\0")

tiles = []
for p in a.frames:
    w, h, px = load(p)
    s = a.scale
    tw, th = w // s, h // s
    out = bytearray(tw * th * 3)
    for y in range(th):
        row = (y * s) * w * 3
        for x in range(tw):
            i = row + x * s * 3          # nearest sample: fast enough for a preview
            out[(y * tw + x) * 3:(y * tw + x) * 3 + 3] = px[i:i + 3]
    tiles.append((tw, th, out))

tw, th = tiles[0][0], tiles[0][1]
cols = min(a.cols, len(tiles))
rows = (len(tiles) + cols - 1) // cols
W, H = cols * tw, rows * th
img = bytearray(W * H * 3)
for k, (_, _, t) in enumerate(tiles):
    ox, oy = (k % cols) * tw, (k // cols) * th
    for y in range(th):
        img[((oy + y) * W + ox) * 3:((oy + y) * W + ox + tw) * 3] = t[y * tw * 3:(y + 1) * tw * 3]

raw = b"".join(b"\0" + bytes(img[y * W * 3:(y + 1) * W * 3]) for y in range(H))
chunk = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
open(a.out, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                       + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
print(f"{a.out}: {len(tiles)} frames, {W}x{H}")
