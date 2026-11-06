#!/usr/bin/env python3
"""The pictures Pause > Controls shows for pads that are not PlayStation's.

The game draws a DualShock 3 (menu/pause/textures/controller_image, 512x512):
light grey lines on a dark translucent body, labels placed by the screen
around it (Move and Look under the sticks). These are our own line drawings
in that style: an Xbox pad (also used for the 360, generic pads and the Steam
Deck), a Switch Pro Controller, and the keyboard with a mouse. The pads'
outer contours are the outlines of Kenney's controller icons (Input Prompts
1.5, CC0), which carry the real proportions; everything on them is ours,
placed from photographs of the controllers.

The keyboard's keys and the mouse's buttons light up for the inputs
kz1hd_keys.ini binds, so the game composes that picture at run time from
three renderings of it (every key dim, every key lit, every key lit orange
for movement) and a map of which input each texel belongs to.

Writes into port/kz1hd/pads/ (zlib-compressed, as glyphs.cpp embeds them):
  xbox.rgba.z, switch.rgba.z      512x512 RGBA, straight alpha
  kbm_dim/_on/_hot.rgba.z         the same, for the keyboard and mouse
  kbm_regions.z                   512x512 bytes: 0 none, else 1 + index into
                                  kbm_regions.txt (one input name per line)
and with --svg <dir> the SVGs too, to look at.

Usage: tools/glyphs/controller_art.py <unzipped Kenney pack dir> [--svg dir]
Needs rsvg-convert, Pillow and numpy.
"""
import math, os, re, subprocess, sys, tempfile, zlib
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "../../port/kz1hd/pads")
LINE = "#c5c5c5"
HOT = "#e8933a"
HEAD = f'''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<defs>
 <radialGradient id="body" cx="0.5" cy="0.45" r="0.6">
  <stop offset="0" stop-color="#1a1c1a" stop-opacity="0.30"/>
  <stop offset="1" stop-color="#0b0c0b" stop-opacity="0.42"/>
 </radialGradient>
 <style>
  .o {{ fill:url(#body); stroke:{LINE}; stroke-width:2.7; stroke-linejoin:round }}
  .b {{ fill:#0d0e0d; fill-opacity:0.35; stroke:{LINE}; stroke-width:1.6; stroke-linejoin:round }}
  .l {{ fill:none; stroke:{LINE}; stroke-width:1.6; stroke-linejoin:round; stroke-linecap:round }}
  .t {{ fill:{LINE}; font-family:'DejaVu Sans'; font-weight:bold; text-anchor:middle; dominant-baseline:central }}
  .k {{ fill:{LINE}; stroke:none }}
 </style>
</defs>
'''

# ---- shapes ------------------------------------------------------------------

def circ(cx, cy, r, c="l"): return f'<circle class="{c}" cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}"/>'
def rrect(x, y, w, h, r, c="l"): return f'<rect class="{c}" x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{r:.1f}"/>'
def path(d, c="l"): return f'<path class="{c}" d="{d}"/>'

def text(x, y, s, size, col=None):
    st = f' style="fill:{col}"' if col else ""
    return f'<text class="t" x="{x:.1f}" y="{y:.1f}" font-size="{size}"{st}>{s}</text>'

def tri(cx, cy, s, d):
    """A small filled arrow pointing d (0 up, 1 right, 2 down, 3 left)."""
    pts = [(0, -s), (s * 0.9, s * 0.6), (-s * 0.9, s * 0.6)]
    a = d * math.pi / 2
    p = " ".join(f"{cx + x * math.cos(a) - y * math.sin(a):.1f},{cy + x * math.sin(a) + y * math.cos(a):.1f}" for x, y in pts)
    return f'<polygon class="k" points="{p}"/>'

def dpad_cross(cx, cy, arm, reach):
    """A d-pad's cross, each arm with its own pad and a gap to the centre."""
    a, r = arm / 2, reach
    s = path(f"M{cx-a},{cy-r} h{arm} v{r-a} h{r-a} v{arm} h{-(r-a)} v{r-a} h{-arm} v{-(r-a)} h{-(r-a)} v{-arm} h{r-a} z", "b")
    g = 4
    for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        if dx == 0:
            y0, y1 = cy + dy * (a + g), cy + dy * (r - 3)
            s += rrect(cx - a + 2.5, min(y0, y1), arm - 5, abs(y1 - y0), 2)
        else:
            x0, x1 = cx + dx * (a + g), cx + dx * (r - 3)
            s += rrect(min(x0, x1), cy - a + 2.5, abs(x1 - x0), arm - 5, 2)
    return s

def stick(cx, cy, r):
    """A stick as the DualShock draws it: its well, the cap, the cap's dish."""
    return circ(cx, cy, r + 5) + circ(cx, cy, r, "b") + circ(cx, cy, r * 0.66)

def letter_button(x, y, r, l, col=None):
    return circ(x, y, r, "b") + circ(x, y, r - 2.6) + text(x, y + 0.5, l, r * 1.0, col)

# ---- Kenney's outlines ---------------------------------------------------------

def kenney_outline(svgpath):
    """The largest subpath (M/L/Q) of a Kenney icon: its outer contour."""
    d = re.search(r' d="([^"]+)"', open(svgpath).read()).group(1)
    toks = re.findall(r"[MLQ]|-?\d+\.?\d*", d)
    subs, cur, cmd, i = [], None, None, 0
    while i < len(toks):
        t = toks[i]
        if t in "MLQ": cmd = t; i += 1; continue
        if cmd == "M":
            cur = [("M", float(toks[i]), float(toks[i + 1]))]; subs.append(cur); i += 2; cmd = "L"
        elif cmd == "L":
            cur.append(("L", float(toks[i]), float(toks[i + 1]))); i += 2
        else:
            cur.append(("Q", *map(float, toks[i:i + 4]))); i += 4
    return max(subs, key=lambda sp: max(v for seg in sp for v in seg[1::2]) - min(v for seg in sp for v in seg[1::2]))

def outline_d(sub, kx0, ky0, sc, ox, oy):
    """The contour as a path in our units: (k - k0) * sc + o."""
    X = lambda v: (v - kx0) * sc + ox
    Y = lambda v: (v - ky0) * sc + oy
    d = ""
    for seg in sub:
        if seg[0] in "ML": d += f"{seg[0]}{X(seg[1]):.1f} {Y(seg[2]):.1f} "
        else: d += f"Q{X(seg[1]):.1f} {Y(seg[2]):.1f} {X(seg[3]):.1f} {Y(seg[4]):.1f} "
    return d + "Z"

def top_edge(d):
    """The y of the top edge of the closed path d (M/L/Q) at x."""
    t = re.findall(r"[MLQZ]|-?\d+\.?\d*", d)
    pts, i, cur, cmd = [], 0, None, None
    while i < len(t):
        if t[i] in "MLQZ": cmd = t[i]; i += 1; continue
        if cmd in "ML": cur = (float(t[i]), float(t[i + 1])); pts.append(cur); i += 2
        elif cmd == "Q":
            c, e = (float(t[i]), float(t[i + 1])), (float(t[i + 2]), float(t[i + 3]))
            for k in range(1, 13):
                u = k / 12
                pts.append(((1 - u) ** 2 * cur[0] + 2 * (1 - u) * u * c[0] + u * u * e[0],
                            (1 - u) ** 2 * cur[1] + 2 * (1 - u) * u * c[1] + u * u * e[1]))
            cur = e; i += 4
        else: i += 1
    segs = list(zip(pts, pts[1:] + pts[:1]))
    return lambda x: min(a[1] + (b[1] - a[1]) * (x - a[0]) / (b[0] - a[0])
                         for a, b in segs if (a[0] - x) * (b[0] - x) <= 0 and a[0] != b[0])

def shoulders(d, xl, xr, xo, peek, band):
    """A pad's shoulders seen from the front, from its outline d: the
    triggers, closed tabs rising `peek` over the outer part of each top
    corner (drawn before the body), and the bumpers, the band `band` deep
    under the corners' top edge (after it). xl/xr: where the corners end
    towards the middle; xo: where the left trigger starts."""
    y = top_edge(d)
    behind = ""
    for x0, x1 in ((xo, xl - 16), (xr + 16, 380 - xo)):
        n = 24
        xs = [x0 + (x1 - x0) * k / n for k in range(n + 1)]
        up = ([(xs[0], y(xs[0]))] +
              [(x, y(x) - peek * (0.78 + 0.22 * min(1, min(k, n - k) / 3) ** 0.5)) for k, x in enumerate(xs)] +
              [(xs[-1], y(xs[-1]))])
        down = [(x, y(x) + 0.5) for x in reversed(xs)]
        behind += '<path class="b" d="M' + " L".join(f"{p[0]:.1f} {p[1]:.1f}" for p in up + down) + ' Z"/>'
    cid = "sh%d" % (int(xl * 10) % 100000)
    front = (f'<clipPath id="{cid}b"><path d="{d}"/></clipPath>'
             f'<clipPath id="{cid}s"><rect x="0" y="0" width="{xl}" height="512"/><rect x="{xr}" y="0" width="512" height="512"/></clipPath>'
             f'<g clip-path="url(#{cid}b)"><g clip-path="url(#{cid}s)"><path class="l" transform="translate(0 {band})" d="{d}"/></g></g>')
    front += "".join(path(f"M{x:.1f} {y(x):.1f} L{x:.1f} {y(x) + band:.1f}") for x in (xl, xr))
    return behind, front

# ---- the pads -------------------------------------------------------------------
# Hole centres are in the 300 px-wide render of Kenney's icon (its 64-unit
# viewbox times 300/64) they were measured on; P maps them to the texture.

def xbox(pack):
    sub = kenney_outline(pack + "/Xbox Series/Vector/controller_xboxseries.svg")
    k = 300 / 64
    S = 377 / 225                      # a little wider than the DualShock
    T = 296 - 154 * S                  # the grips end where the DualShock's do
    P = lambda x, y: (190 + (x - 150) * S, T + (y - 70) * S)
    d = outline_d(sub, 37 / k, 70 / k, k * S, P(37, 0)[0], T)
    behind, front = shoulders(d, P(108.5, 0)[0], P(190.5, 0)[0], P(68, 0)[0], 17, 12)
    s = behind + f'<path class="o" d="{d}"/>' + front
    x, y = P(96, 110); s += stick(x, y, 24.5)
    x, y = P(119, 152); s += circ(x, y, 30) + circ(x, y, 25.5, "b")
    s += dpad_cross(x, y, 11, 21)      # the hybrid d-pad: a cross on a faceted dish
    for dx, dy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        s += path(f"M{x+dx*7.7:.1f} {y+dy*7.7:.1f} L{x+dx*16:.1f} {y+dy*16:.1f}")
    x, y = P(168, 150); s += stick(x, y, 23.5)
    x, y = P(150, 93); s += circ(x, y, 15.5) + circ(x, y, 11, "b")     # guide
    for (kx, ky), l, c in (((201, 98), "Y", "#e8c33c"), ((182, 117), "X", "#4f8fe8"),
                           ((220, 117), "B", "#e6533f"), ((201, 135), "A", "#5fc34f")):
        x, y = P(kx, ky); s += letter_button(x, y, 13.5, l, c)
    x, y = P(134, 112); s += circ(x, y, 7.5, "b") + rrect(x - 4, y - 3.5, 5.5, 4.5, 0.8) + rrect(x - 1.5, y - 1, 5.5, 4.5, 0.8)   # view
    x, y = P(166, 112); s += circ(x, y, 7.5, "b") + path(f"M{x-3.5} {y-2.5} h7 M{x-3.5} {y} h7 M{x-3.5} {y+2.5} h7")         # menu
    x, y = P(150, 124); s += rrect(x - 7, y - 4, 14, 8, 4, "b") + path(f"M{x} {y+2} v-4 M{x-2} {y-0.5} L{x} {y-2.5} L{x+2} {y-0.5}")   # share
    s += circ(190, P(0, 172)[1], 2.5)                                  # headset jack
    return s

def switch(pack):
    sub = kenney_outline(pack + "/Nintendo Switch/Vector/controller_switch_pro.svg")
    k = 300 / 64
    S = 377 / 223
    T = 296 - 154 * S
    P = lambda x, y: (190 + (x - 149.5) * S, T + (y - 70) * S)
    d = outline_d(sub, 38 / k, 70 / k, k * S, P(38, 0)[0], T)
    behind, front = shoulders(d, P(110, 0)[0], P(189, 0)[0], P(70, 0)[0], 17, 12)
    s = behind + f'<path class="o" d="{d}"/>' + front
    x, y = P(88, 112); s += stick(x, y, 24.5)
    x, y = P(117, 150); s += dpad_cross(x, y, 15, 25)
    for i, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):   # its arrows are on the arms
        s += tri(x + dx * 17, y + dy * 17, 2.6, i)
    x, y = P(178, 149); s += stick(x, y, 24.5)
    for (kx, ky), l in (((210, 103), "X"), ((192, 117), "Y"), ((229, 117), "A"), ((210, 131), "B")):
        x, y = P(kx, ky); s += letter_button(x, y, 13, l)
    x, y = P(128, 98); s += circ(x, y, 5.5, "b") + path(f"M{x-3} {y} h6")                        # minus
    x, y = P(171, 98); s += circ(x, y, 5.5, "b") + path(f"M{x-3} {y} h6 M{x} {y-3} v6")           # plus
    x, y = P(136, 120); s += rrect(x - 5, y - 5, 10, 10, 2, "b") + circ(x, y, 2.6)                # capture
    x, y = P(163, 120); s += circ(x, y, 6.5, "b") + path(f"M{x-3} {y+2.5} V{y-0.5} L{x} {y-3} L{x+3} {y-0.5} V{y+2.5}")   # home
    return s

# ---- keyboard and mouse -------------------------------------------------------------
# mode: "dim" (every key dim, nothing lit), "on" (all lit), "hot" (all lit in
# the movement colour), or "regions" (each input's area in its own colour).

REGIONS = []   # input names, in the order of their region colours

def region(name):
    if name not in REGIONS: REGIONS.append(name)
    return f"rgb({REGIONS.index(name) + 1},0,0)"

def lit(mode):
    return (HOT, 0.55) if mode == "hot" else ("#ffffff", 0.30)

def keycap(x, y, w, h, name, key, mode):
    """A keycap from above: its skirt and the dished top."""
    if mode == "regions":
        return f'<rect x="{x-1:.1f}" y="{y-1:.1f}" width="{w+2:.1f}" height="{h+2:.1f}" style="fill:{region(key)}"/>'
    dim = ' style="opacity:0.45"' if mode == "dim" else ""
    s = f'<g{dim}>' + rrect(x, y, w, h, 3.5, "b")
    if mode != "dim":
        col, op = lit(mode)
        s += f'<rect x="{x+2.5:.1f}" y="{y+2:.1f}" width="{w-5:.1f}" height="{h-5.5:.1f}" rx="2.5" style="fill:{col};fill-opacity:{op}"/>'
    s += rrect(x + 2.5, y + 2, w - 5, h - 5.5, 2.5)
    size = 10 if len(name) == 1 else 6.5
    return s + text(x + w / 2, y + h / 2 - 1.2, name, size) + '</g>'

def kbm(mode):
    """The left of a keyboard (fading out to the right) under Move and a
    mouse under Look."""
    s = ""
    U, G = 23, 3
    x0, y0 = 12, 108
    # (label, width in key units, name in kz1hd_keys.ini)
    rows = [
        [("Esc", 1, "ESCAPE"), (None, 0.6, None), ("F1", 1, "F1"), ("F2", 1, "F2"), ("F3", 1, "F3"), ("F4", 1, "F4"), (None, 0.4, None), ("F5", 1, "F5")],
        [("`", 1, "`"), ("1", 1, "1"), ("2", 1, "2"), ("3", 1, "3"), ("4", 1, "4"), ("5", 1, "5"), ("6", 1, "6"), ("7", 1, "7")],
        [("Tab", 1.5, "TAB")] + [(c, 1, c) for c in "QWERTY"],
        [("Caps", 1.75, "CAPSLOCK")] + [(c, 1, c) for c in "ASDFGH"],
        [("Shift", 2.25, "SHIFT")] + [(c, 1, c) for c in "ZXCVB"],
        [("Ctrl", 1.25, "CTRL"), ("Win", 1.25, "WIN"), ("Alt", 1.25, "ALT"), ("Space", 4, "SPACE")],
    ]
    right = 214
    if mode != "regions":
        s += ('<defs><linearGradient id="fade" x1="0" x2="1"><stop offset="0.78" stop-color="#fff"/>'
              '<stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>'
              f'<mask id="fadeout"><rect x="0" y="0" width="{right}" height="512" fill="url(#fade)"/></mask></defs>')
        s += '<g mask="url(#fadeout)">'
        top, bottom = y0 - 14, y0 + 6 * (U + G) + 10
        s += (f'<path class="o" d="M{right+20} {top} L{x0-2} {top} C{x0-10} {top} {x0-12} {top+2} {x0-12} {top+10} '
              f'L{x0-12} {bottom-8} C{x0-12} {bottom} {x0-10} {bottom} {x0-2} {bottom} L{right+20} {bottom}"/>')
        s += path(f"M{x0-6} {top+8} L{right+20} {top+8}")
    for r, row in enumerate(rows):
        x = x0; y = y0 + r * (U + G) + (0 if r == 0 else 6)
        for name, w, key in row:
            kw = w * U + (w - 1) * G
            if name: s += keycap(x, y, kw, U - (4 if r == 0 else 0), name, key, mode)
            x += kw + G
    if mode != "regions": s += '</g>'

    # the mouse: two buttons either side of the wheel's channel, side buttons
    # along its left flank, the cable out of the front
    cx, my, mw, mh = 268, 112, 88, 156
    L, R = cx - mw / 2, cx + mw / 2
    body = (f"M{cx} {my} C{cx+28} {my} {R} {my+14} {R} {my+52} L{R} {my+96} "
            f"C{R} {my+136} {cx+26} {my+mh} {cx} {my+mh} C{cx-26} {my+mh} {L} {my+136} {L} {my+96} "
            f"L{L} {my+52} C{L} {my+14} {cx-28} {my} {cx} {my} Z")
    split = my + 66
    btn_l = f"M{L-2} {my-2} L{cx-8} {my-2} L{cx-8} {split-6} C{cx-20} {split} {L+10} {split} {L-2} {split-2} Z"
    btn_r = f"M{R+2} {my-2} L{cx+8} {my-2} L{cx+8} {split-6} C{cx+20} {split} {R-10} {split} {R+2} {split-2} Z"
    wheel = (cx - 6.5, my + 12, 13, 30)
    sides = [(my + 76, "MOUSE5"), (my + 98, "MOUSE4")]
    if mode == "regions":
        s += f'<clipPath id="mouse"><path d="{body}"/></clipPath><g clip-path="url(#mouse)">'
        s += f'<path d="{btn_l}" style="fill:{region("MOUSELEFT")}"/><path d="{btn_r}" style="fill:{region("MOUSERIGHT")}"/></g>'
        s += f'<rect x="{wheel[0]-1}" y="{wheel[1]-1}" width="{wheel[2]+2}" height="{wheel[3]+2}" style="fill:{region("WHEEL")}"/>'
        for y, key in sides:
            s += f'<rect x="{L}" y="{y}" width="9" height="18" style="fill:{region(key)}"/>'
        return s
    col, op = lit(mode)
    fill = f"fill:{col};fill-opacity:{op}" if mode != "dim" else "fill:none"
    s += f'<path class="o" d="{body}"/><clipPath id="mouse"><path d="{body}"/></clipPath><g clip-path="url(#mouse)">'
    for d in (btn_l, btn_r):
        s += f'<path d="{d}" style="{fill}"/><path class="l" d="{d}"/>'
    s += '</g>'
    s += rrect(*wheel, 6.5, "b")
    if mode != "dim":
        s += f'<rect x="{cx-4.5}" y="{my+14}" width="9" height="26" rx="4.5" style="{fill}"/>'
    s += path(" ".join(f"M{cx-4} {my+18+i*4} h8" for i in range(6)))                                  # wheel
    s += rrect(cx - 3, my + 47, 6, 9, 3)                                                              # DPI
    for y, key in sides:
        s += f'<path class="b" d="M{L+1.5} {y} C{L+7} {y} {L+8} {y+2} {L+8} {y+9} C{L+8} {y+16} {L+7} {y+18} {L+1.5} {y+18}"/>'
        if mode != "dim":
            s += f'<rect x="{L+2}" y="{y+2}" width="4.5" height="14" rx="2" style="{fill}"/>'
    s += path(f"M{L+16} {my+mh-30} C{cx-14} {my+mh-14} {cx+14} {my+mh-14} {R-16} {my+mh-30}")       # palm seam
    s += path(f"M{cx} {my} C{cx} {my-12} {cx+6} {my-18} {cx+16} {my-22} C{cx+28} {my-27} {cx+32} {my-36} {cx+30} {my-48}")   # cable
    return s

# ---- baking ---------------------------------------------------------------------------

def render(svg, size, crisp=False):
    with tempfile.TemporaryDirectory() as td:
        src, png = os.path.join(td, "a.svg"), os.path.join(td, "a.png")
        if crisp: svg = svg.replace("<svg ", '<svg shape-rendering="crispEdges" ', 1)
        open(src, "w").write(svg)
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), src, "-o", png], check=True)
        return np.asarray(Image.open(png).convert("RGBA"), dtype=np.float64)

def bake(svg):
    """4x supersampled, box-filtered with premultiplied alpha, straight RGBA out."""
    a = render(svg, 2048)
    pm = (a[..., :3] * a[..., 3:4] / 255).reshape(512, 4, 512, 4, 3).mean((1, 3))
    al = a[..., 3].reshape(512, 4, 512, 4).mean((1, 3))
    rgb = np.where(al[..., None] > 0, pm * 255 / np.maximum(al[..., None], 1e-6), 0)
    return np.dstack([np.clip(np.round(rgb), 0, 255), np.round(al)]).astype(np.uint8)

def main():
    args = sys.argv[1:]
    if not args: sys.exit(__doc__)
    pack, svgdir = args[0], args[args.index("--svg") + 1] if "--svg" in args else None
    os.makedirs(OUT, exist_ok=True)
    pics = {"xbox": xbox(pack), "switch": switch(pack),
            "kbm_dim": kbm("dim"), "kbm_on": kbm("on"), "kbm_hot": kbm("hot")}
    for name, body in pics.items():
        svg = HEAD + body + "</svg>\n"
        if svgdir: open(os.path.join(svgdir, name + ".svg"), "w").write(svg)
        open(os.path.join(OUT, name + ".rgba.z"), "wb").write(zlib.compress(bake(svg).tobytes(), 9))
    reg = render('<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">'
                 + kbm("regions") + "</svg>", 512, crisp=True)
    ids = np.where(reg[..., 3] > 127, reg[..., 0], 0).astype(np.uint8)
    open(os.path.join(OUT, "kbm_regions.z"), "wb").write(zlib.compress(ids.tobytes(), 9))
    open(os.path.join(OUT, "kbm_regions.txt"), "w").write("\n".join(REGIONS) + "\n")
    for f in sorted(os.listdir(OUT)): print(f"{os.path.getsize(os.path.join(OUT, f)):8d} {f}")

if __name__ == "__main__":
    main()
