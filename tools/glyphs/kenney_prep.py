#!/usr/bin/env python3
"""Kenney Input Prompts (CC0) -> the shapes glyphs.cpp paints in the game's own style.

Each prompt in the pack is white: a button's silhouette with its symbol cut
out (a letter, an arrow), and a few mark the part meant (the d-pad's arm, the
mouse button) in red. For every entry of kenney_map.txt this splits it into
signed distance fields, written to port/kz1hd/glyphs_kenney.inc:

  body  the silhouette with its holes filled (what the game's base paints:
        fill, ring and shadow, measured from its own buttons at run time)
  sym   the holes: the symbol, painted on top in the button's colour (with
        " +close" after its path, cuts that reach the edge, like a mouse's
        button lines, count as holes too)
  hot   the red part, if any, painted in the highlight colour

A field is N x N bytes, 128 at the edge, +PER_TEXEL per field texel inside.
The game's cells are at most 56 texels tall; a distance field sampled
bilinearly keeps its edges sharp when enlarged, so 48x48 is enough. Fields
that come out byte for byte the same (every letter key's cap) are written once.

Usage: tools/glyphs/kenney_prep.py <unzipped pack dir> [out.inc]
Needs rsvg-convert (vector sources) or Pillow (PNG sources) and numpy.
"""
import os, subprocess, sys, tempfile
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RASTER = 480          # rasterise at this size, then measure distances
N = 48                # field size written out
PER_TEXEL = 16        # field units per output texel (so +-8 texels fit in a byte)


def load_rgba(path):
    if path.endswith(".svg"):
        with tempfile.TemporaryDirectory() as td:
            png = os.path.join(td, "a.png")
            subprocess.run(["rsvg-convert", "-w", str(RASTER), "-h", str(RASTER), "-a",
                            "-b", "transparent", path, "-o", png], check=True)
            img = Image.open(png).convert("RGBA")
    else:
        img = Image.open(path).convert("RGBA")
    # square canvas, the icon centred (most are square already)
    s = max(img.size)
    sq = Image.new("RGBA", (s, s))
    sq.paste(img, ((s - img.size[0]) // 2, (s - img.size[1]) // 2))
    sq = sq.resize((RASTER, RASTER), Image.LANCZOS)
    return np.asarray(sq, dtype=np.float32) / 255.0


def fill_holes(m):
    """m with every region not connected to the border set (flood fill)."""
    h, w = m.shape
    outside = np.zeros_like(m)
    stack = [(y, x) for x in range(w) for y in (0, h - 1)] + [(y, x) for y in range(h) for x in (0, w - 1)]
    stack = [(y, x) for y, x in stack if not m[y, x]]
    for y, x in stack:
        outside[y, x] = True
    while stack:
        y, x = stack.pop()
        for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= yy < h and 0 <= xx < w and not m[yy, xx] and not outside[yy, xx]:
                outside[yy, xx] = True
                stack.append((yy, xx))
    return ~outside


def close(m, r):
    """m with gaps narrower than 2r bridged (dilate, then erode)."""
    grown = edt(m) <= r
    return ~(edt(~grown) <= r)


def edt(m):
    """Euclidean distance from each False texel to the nearest True one
    (Felzenszwalb-Huttenlocher, separable)."""
    INF = 1e20
    f = np.where(m, 0.0, INF)

    def pass1d(g):
        n = len(g); d = np.empty(n); v = np.zeros(n, dtype=int); z = np.empty(n + 1)
        k = 0; z[0] = -INF; z[1] = INF
        for q in range(1, n):
            while True:
                s = ((g[q] + q * q) - (g[v[k]] + v[k] * v[k])) / (2 * q - 2 * v[k])
                if s <= z[k]:
                    k -= 1
                    continue
                break
            k += 1; v[k] = q; z[k] = s; z[k + 1] = INF
        k = 0
        for q in range(n):
            while z[k + 1] < q:
                k += 1
            d[q] = (q - v[k]) ** 2 + g[v[k]]
        return d

    f = np.apply_along_axis(pass1d, 0, f)
    f = np.apply_along_axis(pass1d, 1, f)
    return np.sqrt(f)


def sdf(m):
    """Signed distance in raster texels, positive inside."""
    if not m.any():
        return np.full(m.shape, -1e3, dtype=np.float32)
    return (edt(~m) - edt(m)).astype(np.float32)   # inside: distance to outside


def to_field(d):
    # box-sample down to N x N, convert to output texels and bytes
    k = RASTER // N
    d = d.reshape(N, k, N, k).mean(axis=(1, 3)) / k
    return np.clip(np.round(128 + d * PER_TEXEL), 0, 255).astype(np.uint8)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    pack = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "../../port/kz1hd/glyphs_kenney.inc")
    entries = []
    for line in open(os.path.join(HERE, "kenney_map.txt")):
        line = line.split("#")[0].strip()
        if not line:
            continue
        name, src = (t.strip() for t in line.split("=", 1))
        bridge = 0
        if " +close" in src:   # cut lines that reach the edge belong to the symbol
            src, bridge = src.replace(" +close", ""), RASTER // 30
        px = load_rgba(os.path.join(pack, src))
        a = px[..., 3] > 0.5
        body = fill_holes(close(a, bridge) if bridge else a)
        sym = body & ~a
        hot = a & (px[..., 0] - px[..., 1] > 0.3)   # red, not white
        fields = [to_field(sdf(m)) if m.any() else None for m in (body, sym, hot)]
        entries.append((name, src, *fields))
        print(f"{name:24s} {src}", file=sys.stderr)

    with open(out, "w") as f:
        f.write("/* Generated by tools/glyphs/kenney_prep.py from Kenney's Input Prompts\n"
                " * (kenney.nl, CC0 1.0): signed distance fields of each prompt's\n"
                f" * silhouette, symbol and highlight, {N}x{N}, 128 at the edge, {PER_TEXEL} per texel. */\n\n")
        seen = {}   # field bytes -> array name
        refs = []
        for name, src, b, s_, h in entries:
            r = []
            for tag, fld in (("body", b), ("sym", s_), ("hot", h)):
                if fld is None:
                    r.append("nullptr")
                    continue
                key = fld.tobytes()
                if key not in seen:
                    seen[key] = f"kk_{name}_{tag}"
                    f.write(f"static const uint8_t {seen[key]}[{N * N}] = {{\n")
                    flat = fld.flatten()
                    for i in range(0, len(flat), 48):
                        f.write("    " + ",".join(str(v) for v in flat[i:i + 48]) + ",\n")
                    f.write("};\n")
                r.append(seen[key])
            refs.append((name, src, r))
        f.write(f"\nstatic const int kKenneyN = {N}, kKenneyPerTexel = {PER_TEXEL};\n")
        f.write("static const struct KenneyShape { const char* name; const uint8_t *body, *sym, *hot; } kKenney[] = {\n")
        for name, src, r in refs:
            f.write(f"    {{ \"{name}\", {r[0]}, {r[1]}, {r[2]} }},   /* {src} */\n")
        f.write("};\n")


if __name__ == "__main__":
    main()
