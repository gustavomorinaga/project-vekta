#!/usr/bin/env python3
"""The project's artwork, all drawn here: the README banner, GitHub's social preview, the README dividers, the setup
program's wizard and header images, and the launcher's, setup's and uninstaller's icons.

    tools/release/installer_art.py --fetch-fonts   # Rajdhani (SIL OFL 1.1) into --fonts, from Google Fonts' own
                                                   # repository; refuses a file whose SHA-256 is not FONTS'
    tools/release/installer_art.py                 # render every image and art.json (needs the fonts and Pillow)
    tools/release/installer_art.py --check         # exit 1 when an image is missing, changed or stale
    tools/release/installer_art.py --preview F.png # also write a contact sheet of every image (not shipped)

The look is the one Project Helghast and Project Pyrrhus share (undecuple's Killzone 3 and Killzone 2 PC ports, MIT):
a dark panel with HUD corner brackets, an accent tag "KILLZONE HD // UNOFFICIAL PC PORT", the wordmark PROJECT in
white and VEKTA in the accent, "BUILT ON YOUR PC / FROM YOUR OWN GAME", a motif line over hazard stripes, and icon
tiles lettered KZ1 over a PC bar. The layout, the image set, the sizes, the icon's size rules and the art.json/--check
scheme follow their tools/release/installer_art.py and draw_dividers.py; the drawing code, the palette (steel blue,
site/style.css), the V chevron emblem and the ridge line are this project's. Nothing here is game art: shapes,
stripes and text only. The fonts are not committed or shipped, only these images (NOTICE.md lists them).

Outputs, installer/art/: wizard-{100,125,150,200}.bmp (Welcome and Finish pages, 24-bit), header-{...}.bmp (the small
image at the top right of the other pages), vekta.ico (the launcher's and KZ1HD.exe's), vekta-setup.ico (with an
arrow badge), vekta-uninstall.ico (with a minus badge), 16 to 256 px, and art.json (every file's SHA-256, this
script's, the fonts', the Pillow and FreeType versions).
Outputs, assets/: banner.png (1280x320) and banner@2x.png (README; opaque panel with transparent chamfers, so it reads
on GitHub's light and dark pages), social-preview.png (1280x640, under 1 MB, every mark 40 px inside the edge),
divider-{frame,rule,hazard}.png (900 px wide, transparent, mid-tone colours for both GitHub themes), icon.png (256 px)
and icon.svg (the same 256 px image inside an SVG, for the Linux menu entry and the payload, which name icon.svg).
--check: with the fonts there it renders again and compares bytes; without them it checks the files against art.json
and that art.json was made by this exact script. The palette must still be in site/style.css. Deterministic: the same
script, fonts, Pillow and FreeType give the same bytes.
Sizes: Inno Setup 6's own for 100/125/150/200 % (its help, WizardImageFile and WizardSmallImageFile).
"""
import argparse
import base64
import hashlib
import io
import json
import os
import pathlib
import re
import struct
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
ART = ROOT / "installer" / "art"
PUBLIC = ROOT / "assets"
CSS = ROOT / "site" / "style.css"
FONT_DIR = ROOT / "work" / "fonts"
FONT_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/rajdhani/"
FONTS = {   # the same files and pins as Project Helghast's and Project Pyrrhus's art.json
    "Rajdhani-Bold.ttf": "691470dd3286a14e9677940d0bf75796179841ba5215cbda1a2c8910a3226afd",
    "Rajdhani-Medium.ttf": "12ff7dcfe4c206e3875ac53b1762eab57de6a2fa7f5a86c26b97b88d6591eac2",
}
ROLES = {"head": "Rajdhani-Bold.ttf", "small": "Rajdhani-Medium.ttf"}

SITE = "gustavomorinaga.github.io/project-vekta"
TAG = "UNOFFICIAL PC PORT"
TAG_WIDE = "KILLZONE HD // UNOFFICIAL PC PORT"
BLURB = ("BUILT ON YOUR PC", "FROM YOUR OWN GAME")
LABEL = ("KZ", "1")         # the icon's letters: white, then the accent; up to 24 px only the accent part is drawn

# site/style.css :root (the dark theme). steel = the accent, steel-hi = its light tone, steel-lo = its tone for light
# pages (dividers), glow = the cool haze at the top of a panel.
PAL = {"bg": "#0a0d10", "panel": "#11161c", "line": "#25313c", "steel": "#6ea8d8", "steel-hi": "#a9d0ee",
       "steel-lo": "#3f7fb5", "text": "#eef2f5", "dim": "#9ba7b3", "glow": "#13222e", "white": "#f4f7f9",
       "grey": "#a9a59d"}
RIDGE = (0, -0.2, 0.35, -0.6, 0.1, -0.95, 0.25, -0.35, 0.5, -0.1, 0.3, -0.75, 0.05, 0.4, -0.5, 0.2, 0)
CHEVRON = ((0, 0), (14, 0), (32, 22), (50, 0), (64, 0), (32, 40))   # the emblem, a 64 x 40 box
CHEVRON_CORE = ((22, 0), (42, 0), (32, 12))

WIZARD = {100: (164, 314), 125: (192, 386), 150: (246, 459), 200: (328, 604)}
HEADER = {100: (55, 55), 125: (64, 68), 150: (83, 80), 200: (110, 106)}
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
ICONS = {"vekta.ico": None, "vekta-setup.ico": "setup", "vekta-uninstall.ico": "uninstall"}
SOCIAL = (1280, 640)
SOCIAL_SAFE = 40
SOCIAL_MAX_BYTES = 1 << 20          # GitHub refuses a social preview of 1 MB or more
BANNER = {"banner.png": (1280, 320), "banner@2x.png": (2560, 640)}
BANNER_MAX_BYTES = 400 * 1024       # light enough for the README (our limit, as Pyrrhus's)
DIVIDER_W = 900
DIVIDER_MAX_BYTES = 20 * 1024
SS = 4                              # supersampling for the small images: drawn at 4x, reduced once


def rgb(name, a=None):
    h = PAL[name]
    c = (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16))
    return c if a is None else c + (a,)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha_file(p):
    return sha256(pathlib.Path(p).read_bytes())


# ------------------------------------------------------------------------------------------------------- fonts
def fetch_fonts(d):
    d.mkdir(parents=True, exist_ok=True)
    for name, want in list(FONTS.items()) + [("OFL.txt", None)]:
        with urllib.request.urlopen(FONT_URL + name, timeout=60) as r:
            data = r.read()
        got = sha256(data)
        if want and got != want:
            raise SystemExit(f"installer_art: {name}: SHA-256 {got}, not the pinned {want}")
        (d / name).write_bytes(data)
        print(f"fetched {name}")


def fonts_ok(d):
    return all((d / n).is_file() and sha_file(d / n) == h for n, h in FONTS.items())


class Fonts:
    def __init__(self, d):
        from PIL import ImageFont
        self.IF, self.d, self.cache = ImageFont, pathlib.Path(d), {}

    def get(self, role, px):
        key = (role, max(1, int(px)))
        if key not in self.cache:
            self.cache[key] = self.IF.truetype(str(self.d / ROLES[role]), key[1],
                                               layout_engine=self.IF.Layout.BASIC)
        return self.cache[key]


# ----------------------------------------------------------------------------------------------------- drawing
def text_w(font, text, track=0.0):
    if not text:
        return 0
    return sum(font.getlength(c) for c in text) + track * font.size * (len(text) - 1)


def draw_text(d, xy, text, font, fill, track=0.0):
    """Letter by letter (the site's letter-spacing); y is the top of the capitals."""
    x, y = xy
    top = font.getbbox("H")[1]
    for c in text:
        d.text((x, y - top), c, font=font, fill=fill)
        x += font.getlength(c) + track * font.size


def fit(fonts, role, text, max_w, max_cap, track=0.0):
    """The largest size whose capitals fit max_w x max_cap; -> (font, width, cap height)."""
    lo, hi = 1, max(2, int(max_cap * 2))
    while lo < hi:
        mid = (lo + hi + 1) // 2
        f = fonts.get(role, mid)
        b = f.getbbox("H")
        if text_w(f, text, track) <= max_w and b[3] - b[1] <= max_cap:
            lo = mid
        else:
            hi = mid - 1
    f = fonts.get(role, lo)
    b = f.getbbox("H")
    return f, text_w(f, text, track), b[3] - b[1]


def ground(w, h, cx=0.3, ry=0.9):
    """The dark panel with the cool glow from the top (the site's radial-gradient at the top)."""
    from PIL import Image
    rx, ryp = max(1, int(w * 0.9)), max(1, int(h * ry))
    g = Image.radial_gradient("L").resize((2 * rx, 2 * ryp), Image.Resampling.BILINEAR)
    mask = Image.new("L", (w, h), 255)
    left = int(rx - w * cx)
    mask.paste(g.crop((left, ryp, left + w, ryp + min(h, ryp))), (0, 0))
    mask = mask.point(lambda v: min(255, v * 2))
    return Image.composite(Image.new("RGB", (w, h), rgb("bg")), Image.new("RGB", (w, h), rgb("glow")), mask)


def brackets(d, x0, y0, x1, y1, length, thick, color):
    """HUD corners, top left and bottom right."""
    d.rectangle((x0, y0, x0 + length, y0 + thick - 1), fill=color)
    d.rectangle((x0, y0, x0 + thick - 1, y0 + length), fill=color)
    d.rectangle((x1 - length, y1 - thick + 1, x1, y1), fill=color)
    d.rectangle((x1 - thick + 1, y1 - length, x1, y1), fill=color)


def stripes(w, h, period, color, ground_col):
    """A band of slanted hazard stripes, w x h."""
    from PIL import Image, ImageDraw
    band = Image.new("RGB", (w, h), ground_col)
    d = ImageDraw.Draw(band)
    x = -h
    while x < w:
        d.polygon([(x, h), (x + h, 0), (x + h + period / 2, 0), (x + period / 2, h)], fill=color)
        x += period
    return band


def ridge(d, x0, x1, yc, amp, width):
    """The motif line: a jagged horizon (a fixed pattern, no randomness), light accent over a soft halo."""
    n = len(RIDGE) - 1
    pts = [(x0 + (x1 - x0) * i / n, yc + amp * j) for i, j in enumerate(RIDGE)]
    halo = tuple((a + b) // 2 for a, b in zip(rgb("steel"), rgb("bg")))
    d.line(pts, fill=halo, width=max(1, round(width * 2.6)), joint="curve")
    d.line(pts, fill=rgb("steel-hi"), width=max(1, round(width)), joint="curve")


def chevron(d, x, y, w, outer, core):
    """The emblem, a V chevron with a light core, w wide (height w * 40 / 64), top left at x, y."""
    s = w / 64.0
    d.polygon([(x + px * s, y + py * s) for px, py in CHEVRON], fill=outer)
    d.polygon([(x + px * s, y + py * s) for px, py in CHEVRON_CORE], fill=core)


def tag(d, fonts, x, y, cap, max_w, text=TAG):
    f, tw, ch = fit(fonts, "head", text, max_w, cap, 0.06)
    padx, pady = round(0.9 * ch), round(0.55 * ch)
    d.rectangle((x, y, x + tw + 2 * padx, y + ch + 2 * pady), fill=rgb("steel"))
    draw_text(d, (x + padx, y + pady), text, f, rgb("bg"), 0.06)
    return tw + 2 * padx, ch + 2 * pady


def wordmark(d, fonts, x, y, cap, max_w, one_line=False):
    """PROJECT (white) and VEKTA (accent) at one size; stacked, or side by side. -> (width, height)."""
    size = min(fit(fonts, "head", "PROJECT", max_w, cap, 0.06)[0].size, fit(fonts, "head", "VEKTA", max_w, cap, 0.06)[0].size)
    while True:
        f = fonts.get("head", size)
        b = f.getbbox("H")
        ch = b[3] - b[1]
        gap = round(0.28 * ch)
        wp, wv = text_w(f, "PROJECT", 0.06), text_w(f, "VEKTA", 0.06)
        if not one_line or wp + gap + wv <= max_w or size <= 4:
            break
        size -= 1
    if one_line:
        draw_text(d, (x, y), "PROJECT", f, rgb("white"), 0.06)
        draw_text(d, (x + wp + gap, y), "VEKTA", f, rgb("steel"), 0.06)
        return wp + gap + wv, ch
    y2 = y + ch + round(0.22 * ch)
    draw_text(d, (x, y), "PROJECT", f, rgb("white"), 0.06)
    draw_text(d, (x, y2), "VEKTA", f, rgb("steel"), 0.06)
    return max(wp, wv), y2 + ch - y


def finish(img, w, h, chamfer):
    """Reduce to w x h and cut the top-right and bottom-left chamfers out (transparent, anti-aliased)."""
    from PIL import Image, ImageDraw
    W, H = img.size
    mask = Image.new("L", (W, H), 255)
    md = ImageDraw.Draw(mask)
    md.polygon([(W - chamfer, 0), (W, 0), (W, chamfer)], fill=0)
    md.polygon([(0, H - chamfer), (0, H), (chamfer, H)], fill=0)
    out = img.resize((w, h), Image.Resampling.LANCZOS).convert("RGBA")
    out.putalpha(mask.resize((w, h), Image.Resampling.LANCZOS))
    return out


def banner(fonts, w, h):
    """README banner: tag and one-line wordmark on the left, blurb and address on the right, the ridge over the
    stripes along the bottom, HUD frame, chamfered corners."""
    from PIL import ImageDraw
    ss = max(1, 5120 // w)
    W, H = w * ss, h * ss
    u = H / 320.0
    img = ground(W, H, cx=0.3, ry=1.2)
    d = ImageDraw.Draw(img)
    ins = round(8 * u)
    d.rectangle((ins, ins, W - ins - 1, H - ins - 1), outline=rgb("line"), width=max(1, round(2 * u)))
    brackets(d, round(4 * u), round(4 * u), W - round(4 * u) - 1, H - round(4 * u) - 1, round(64 * u),
             round(5 * u), rgb("steel"))
    m = round(56 * u)
    right_w = round(330 * u)
    tag(d, fonts, m, round(46 * u), round(18 * u), W - 2 * m, TAG_WIDE)
    y = round(104 * u)
    ww, ch = wordmark(d, fonts, m, y, round(92 * u), W - 2 * m - right_w - round(40 * u), one_line=True)
    y += ch + round(24 * u)
    d.rectangle((m, y, m + ww, y + round(4 * u) - 1), fill=rgb("steel"))
    rx1 = W - m
    fb, _, cb = fit(fonts, "head", max(BLURB, key=len), right_w, round(22 * u), 0.08)
    by = round(112 * u)
    for line in BLURB:
        draw_text(d, (rx1 - text_w(fb, line, 0.08), by), line, fb, rgb("text"), 0.08)
        by += cb + round(12 * u)
    fu, uw, _ = fit(fonts, "small", SITE, right_w, round(20 * u))
    draw_text(d, (rx1 - uw, by + round(10 * u)), SITE, fu, rgb("dim"))
    sy = H - round(56 * u)
    ridge(d, m, W - m, sy - round(18 * u), round(7 * u), round(1.6 * u))
    img.paste(stripes(W - 2 * m, round(16 * u), round(24 * u), rgb("steel"), rgb("bg")), (m, sy))
    return finish(img, w, h, round(26 * u))


def social_preview(fonts, w=SOCIAL[0], h=SOCIAL[1]):
    """GitHub's social preview: the text column on the left, the chevron on the right, all inside the safe margin."""
    from PIL import Image, ImageDraw
    ss = max(1, 5120 // w)
    W, H = w * ss, h * ss
    u = W / 1280.0
    img = ground(W, H, cx=0.7, ry=0.7)
    d = ImageDraw.Draw(img)
    s0 = round(SOCIAL_SAFE * u)
    d.rectangle((s0 + round(6 * u), s0 + round(6 * u), W - s0 - round(6 * u) - 1, H - s0 - round(6 * u) - 1),
                outline=rgb("line"), width=max(1, round(2 * u)))
    brackets(d, s0, s0, W - s0 - 1, H - s0 - 1, round(90 * u), round(6 * u), rgb("steel"))
    m = round(88 * u)
    emb = round(330 * u)
    ex = W - m - emb
    col_w = ex - m - round(40 * u)
    y = round(92 * u)
    _, th = tag(d, fonts, m, y, round(22 * u), col_w, TAG_WIDE)
    y += th + round(36 * u)
    _, wh = wordmark(d, fonts, m, y, round(84 * u), col_w)
    y += wh + round(28 * u)
    d.rectangle((m, y, m + col_w, y + round(4 * u) - 1), fill=rgb("steel"))
    y += round(24 * u)
    line = "  ·  ".join(BLURB)
    fb, _, _ = fit(fonts, "head", line, col_w + round(40 * u), round(26 * u), 0.08)
    draw_text(d, (m, y), line, fb, rgb("text"), 0.08)
    chevron(d, ex, round(170 * u), emb, rgb("steel"), rgb("steel-hi"))
    y0 = H - s0 - round(112 * u)
    ridge(d, m, W - m, y0 - round(22 * u), round(10 * u), round(2 * u))
    img.paste(stripes(W - 2 * m, round(22 * u), round(28 * u), rgb("steel"), rgb("bg")), (m, y0))
    fu, _, _ = fit(fonts, "small", SITE, col_w, round(24 * u))
    draw_text(d, (m, y0 + round(48 * u)), SITE, fu, rgb("dim"))
    return img.resize((w, h), Image.Resampling.LANCZOS)


def wizard(fonts, w, h):
    """The Welcome and Finish pages' side image: tag, stacked wordmark, game line, blurb, ridge, stripes, address."""
    from PIL import Image, ImageDraw
    W, H = w * SS, h * SS
    u = W / 164.0
    img = ground(W, H)
    d = ImageDraw.Draw(img)
    m = round(14 * u)
    d.rectangle((round(5 * u), round(5 * u), W - round(5 * u) - 1, H - round(5 * u) - 1), outline=rgb("line"),
                width=max(1, round(u)))
    brackets(d, round(4 * u), round(4 * u), W - round(4 * u) - 1, H - round(4 * u) - 1, round(22 * u),
             max(1, round(2 * u)), rgb("steel"))
    y = round(0.07 * H)
    _, th = tag(d, fonts, m, y, round(9 * u), W - 2 * m)
    y += th + round(18 * u)
    _, wh = wordmark(d, fonts, m, y, round(34 * u), W - 2 * m)
    y += wh + round(14 * u)
    d.rectangle((m, y, W - m, y + max(1, round(2 * u)) - 1), fill=rgb("steel"))
    y += round(12 * u)
    f, gw, gh = fit(fonts, "head", "KILLZONE HD", W - 2 * m, round(13 * u), 0.06)
    draw_text(d, (m, y), "KILLZONE", f, rgb("text"), 0.06)
    draw_text(d, (m + text_w(f, "KILLZONE ", 0.06) + 0.06 * f.size, y), "HD", f, rgb("steel-hi"), 0.06)
    y += gh + round(16 * u)
    fb, _, cb = fit(fonts, "head", max(BLURB, key=len), W - 2 * m, round(11 * u), 0.08)
    for line in BLURB:
        draw_text(d, (m, y), line, fb, rgb("dim"), 0.08)
        y += cb + round(7 * u)
    by0, by1 = round(0.80 * H), round(0.80 * H + 12 * u)
    ridge(d, m, W - m, by0 - round(14 * u), round(5 * u), u)
    img.paste(stripes(W - 2 * m, by1 - by0, round(14 * u), rgb("steel"), rgb("bg")), (m, by0))
    y = by1 + round(12 * u)
    host, path = SITE.rsplit("/", 1)
    lines = (host, "/" + path)
    f, _, ch = fit(fonts, "head", max(lines, key=len), W - 2 * m, round(11 * u))
    for line in lines:
        draw_text(d, (m, y), line, f, rgb("text"))
        y += ch + round(6 * u)
    return img.resize((w, h), Image.Resampling.LANCZOS)


def tile(fonts, w, h, badge=None, alpha=False):
    """The icon tile and the header image: KZ1 over a PC bar in HUD corners. Size rules (as the family's): up to
    24 px only the accent's "1" over the bar; at 32 px KZ1 and the bar; from 48 px also "PC" on the bar.
    badge: None, "setup" (light accent square, down arrow) or "uninstall" (grey square, minus)."""
    from PIL import Image, ImageDraw
    W, H = w * SS, h * SS
    s = min(W, H)
    img = ground(W, H, cx=0.3, ry=0.9).convert("RGBA")
    d = ImageDraw.Draw(img)
    if alpha:
        c = round(0.16 * s)
        d.polygon([(W - c, 0), (W, 0), (W, c)], fill=(0, 0, 0, 0))
        d.polygon([(0, H - c), (0, H), (c, H)], fill=(0, 0, 0, 0))
    else:
        d.rectangle((0, 0, W - 1, H - 1), outline=rgb("line"), width=max(1, SS))
    small = min(w, h) <= 24
    brackets(d, 0, 0, W - 1, H - 1, round(0.3 * s), max(SS, round(0.05 * s)), rgb("steel"))
    mx = round(0.12 * W)
    bsz = round((0.5 if small else 0.42) * s)
    gap = max(SS, round(0.05 * s))
    bar_x1 = W - bsz - gap - 1
    if small:
        bar0, bar1 = round(0.62 * H), round(0.86 * H)
        f, _, _ = fit(fonts, "head", LABEL[1], W - 2 * mx, round(0.42 * H))
        draw_text(d, (mx, round(0.10 * H)), LABEL[1], f, rgb("steel-hi"))
    else:
        bar0, bar1 = round(0.64 * H), round(0.86 * H)
        f, _, _ = fit(fonts, "head", "".join(LABEL), W - 2 * mx, round(0.32 * H), 0.04)
        y = round(0.18 * H)
        draw_text(d, (mx, y), LABEL[0], f, rgb("white"), 0.04)
        draw_text(d, (mx + text_w(f, LABEL[0], 0.04) + 0.04 * f.size, y), LABEL[1], f, rgb("steel"))
    d.rectangle((mx, bar0, bar_x1, bar1), fill=rgb("steel"))
    if not small and min(w, h) >= 48:
        pad = round(0.04 * s)
        f, _, ch = fit(fonts, "head", "PC", bar_x1 - mx - 2 * pad, round(0.6 * (bar1 - bar0)), 0.1)
        draw_text(d, (mx + pad, bar0 + (bar1 - bar0 - ch) / 2), "PC", f, rgb("bg"), 0.1)
    if badge:
        x0, y0 = W - bsz, H - bsz
        d.rectangle((x0, y0, W - 1, H - 1), fill=rgb("steel-hi") if badge == "setup" else rgb("grey"))
        cx, cy, r = x0 + bsz / 2, y0 + bsz / 2, bsz * 0.32
        if badge == "setup":
            d.polygon([(cx - r, cy - r * 0.1), (cx + r, cy - r * 0.1), (cx, cy + r)], fill=rgb("bg"))
            d.rectangle((cx - r * 0.38, cy - r, cx + r * 0.38, cy - r * 0.1), fill=rgb("bg"))
        else:
            d.rectangle((cx - r, cy - r * 0.28, cx + r, cy + r * 0.28), fill=rgb("bg"))
    out = img.resize((w, h), Image.Resampling.LANCZOS)
    return out if alpha else out.convert("RGB")


def divider(kind):
    """README dividers, 900 px wide and transparent, in the accent's mid and deep tones (legible on GitHub's light
    and dark pages): 'frame' (corner ticks and a rule with three marks), 'rule' (two rules and the chevron),
    'hazard' (a short band of stripes)."""
    from PIL import Image, ImageDraw
    h = 14 if kind == "hazard" else 28
    W, H = DIVIDER_W * SS, h * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    mid, deep = rgb("steel", 255), rgb("steel-lo", 255)
    soft = rgb("steel", 120)
    t = lambda v: round(v * SS)   # noqa: E731
    if kind == "frame":
        d.line([(t(1), t(27)), (t(1), t(1)), (t(21), t(1))], fill=mid, width=t(2), joint="curve")
        d.line([(t(879), t(27)), (t(899), t(27)), (t(899), t(1))], fill=mid, width=t(2), joint="curve")
        d.rectangle((t(30), t(13), t(870), t(14.5)), fill=soft)
        for x in (436, 448, 460):
            d.rectangle((t(x), t(10), t(x + 4), t(18)), fill=mid)
    elif kind == "rule":
        for x0 in (50, 500):
            d.rectangle((t(x0), t(13), t(x0 + 350), t(14.5)), fill=soft)
        d.rectangle((t(274), t(12.5), t(400), t(15)), fill=deep)
        d.rectangle((t(500), t(12.5), t(626), t(15)), fill=deep)
        chevron(d, t(432), t(3), t(36), deep, mid)
    else:
        d.rectangle((t(320), 0, t(580), H), fill=rgb("line", 255))
        x = 330
        while x < 580:
            d.polygon([(t(x), 0), (t(x + 10), 0), (t(x + 2), H), (t(x - 8), H)], fill=mid)
            x += 20
        clip = Image.new("L", (W, H), 0)
        ImageDraw.Draw(clip).rectangle((t(320), 0, t(580), H), fill=255)
        img.putalpha(Image.composite(img.getchannel("A"), clip, clip))
    return img.resize((DIVIDER_W, h), Image.Resampling.LANCZOS)


# --------------------------------------------------------------------------------------------------- encoding
def bmp_bytes(img):
    buf = io.BytesIO()
    img.convert("RGB").save(buf, format="BMP")
    return buf.getvalue()


def png_bytes(img):
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=False, compress_level=9)
    return buf.getvalue()


def dib(img):
    """One ICO entry as a 32-bit BI_RGB DIB (BGRA rows bottom-up, then the AND mask): what tools/icon_header.py reads."""
    w, h = img.size
    px = img.convert("RGBA").tobytes()
    rows = []
    for y in range(h - 1, -1, -1):
        r = px[y * w * 4:(y + 1) * w * 4]
        rows.append(b"".join(bytes((r[i + 2], r[i + 1], r[i], r[i + 3])) for i in range(0, len(r), 4)))
    stride = ((w + 31) // 32) * 4
    mask = []
    for y in range(h - 1, -1, -1):
        bits = bytearray(stride)
        for x in range(w):
            if px[(y * w + x) * 4 + 3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask.append(bytes(bits))
    body = b"".join(rows) + b"".join(mask)
    return struct.pack("<IiiHHIIiiII", 40, w, 2 * h, 1, 32, 0, len(body), 0, 0, 0, 0) + body


def ico_bytes(images):
    """ICONDIR and entries; 256 px as PNG, the others as DIBs."""
    blobs = [png_bytes(im) if im.size[0] >= 256 else dib(im) for im in images]
    out = struct.pack("<HHH", 0, 1, len(images))
    off = 6 + 16 * len(images)
    for im, b in zip(images, blobs):
        w, h = im.size
        out += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(b), off)
        off += len(b)
    return out + b"".join(blobs)


def svg_wrapping(png):
    return ("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"256\" height=\"256\" viewBox=\"0 0 256 256\">"
            "<title>Project Vekta</title><image width=\"256\" height=\"256\" href=\"data:image/png;base64,"
            + base64.b64encode(png).decode() + "\"/></svg>\n").encode()


# ------------------------------------------------------------------------------------------------------ render
def render(fonts):
    """-> {installer/art file name: bytes}."""
    out = {}
    for pct, (w, h) in WIZARD.items():
        out[f"wizard-{pct}.bmp"] = bmp_bytes(wizard(fonts, w, h))
    for pct, (w, h) in HEADER.items():
        out[f"header-{pct}.bmp"] = bmp_bytes(tile(fonts, w, h))
    for name, badge in ICONS.items():
        out[name] = ico_bytes([tile(fonts, s, s, badge=badge, alpha=True) for s in ICON_SIZES])
    return out


def render_public(fonts):
    """-> {assets/ file name: bytes}."""
    out = {"social-preview.png": png_bytes(social_preview(fonts))}
    for name, (w, h) in BANNER.items():
        out[name] = png_bytes(banner(fonts, w, h))
    for kind in ("frame", "rule", "hazard"):
        out[f"divider-{kind}.png"] = png_bytes(divider(kind))
    icon = png_bytes(tile(fonts, 256, 256, alpha=True))
    out["icon.png"] = icon
    out["icon.svg"] = svg_wrapping(icon)
    return out


def limit(name):
    if name == "social-preview.png":
        return SOCIAL_MAX_BYTES
    if name.startswith("divider-"):
        return DIVIDER_MAX_BYTES
    if name.startswith("banner"):
        return BANNER_MAX_BYTES
    return None


def versions():
    import PIL
    from PIL import features
    return {"pillow": PIL.__version__, "freetype": features.version("freetype2") or "-"}


def manifest(files, public):
    return {"about": "tools/release/installer_art.py's outputs; --check compares against this file",
            "script": sha_file(__file__), **versions(), "fonts": dict(FONTS),
            "files": {n: sha256(b) for n, b in sorted(files.items())},
            "public_files": {n: sha256(b) for n, b in sorted(public.items())}}


def palette_problems():
    css = CSS.read_text(encoding="utf-8").lower()
    return [f"site/style.css no longer has --{k}: {v}" for k, v in PAL.items()
            if not re.search(rf"--{re.escape(k)}:\s*{re.escape(v.lower())}\b", css)]


def check(fonts_dir):
    problems = palette_problems()
    try:
        art = json.loads((ART / "art.json").read_text())
    except (OSError, ValueError) as err:
        return problems + [f"no art.json: {err}"]
    if art.get("script") != sha_file(__file__):
        problems.append("art.json was made by another version of this script: render again")
    want = set(render_names()[0]), set(render_names()[1])
    for key, d, names in (("files", ART, want[0]), ("public_files", PUBLIC, want[1])):
        if set(art.get(key, {})) != names:
            problems.append(f"art.json's {key} list {sorted(art.get(key, {}))}, the script makes {sorted(names)}")
        for n, h in art.get(key, {}).items():
            p = d / n
            if not p.is_file():
                problems.append(f"missing: {p.relative_to(ROOT)}")
            elif sha_file(p) != h:
                problems.append(f"changed: {p.relative_to(ROOT)}")
            elif limit(n) and p.stat().st_size >= limit(n):
                problems.append(f"{p.relative_to(ROOT)} is {p.stat().st_size} bytes, the limit is under {limit(n)}")
    if fonts_ok(fonts_dir) and not problems:
        if versions() != {k: art.get(k) for k in ("pillow", "freetype")}:
            print(f"installer_art: note: Pillow/FreeType {versions()} differ from art.json's; byte compare skipped")
        else:
            fonts = Fonts(fonts_dir)
            for key, fresh in (("files", render(fonts)), ("public_files", render_public(fonts))):
                for n, b in fresh.items():
                    if sha256(b) != art[key].get(n):
                        problems.append(f"renders differently: {n}")
    return problems


def render_names():
    files = [f"wizard-{p}.bmp" for p in WIZARD] + [f"header-{p}.bmp" for p in HEADER] + list(ICONS)
    public = ["social-preview.png", *BANNER, *(f"divider-{k}.png" for k in ("frame", "rule", "hazard")),
              "icon.png", "icon.svg"]
    return files, public


def preview(files, public, path):
    """Every image on a mid grey, then the 16/24/32 px icons enlarged 6x."""
    from PIL import Image
    ims, zoom = [], []
    for n, b in sorted(files.items()) + sorted((k, v) for k, v in public.items() if k.endswith(".png")):
        im = Image.open(io.BytesIO(b))
        if n.endswith(".ico"):
            for s in sorted(im.info.get("sizes", {(256, 256)})):
                im.size = s
                ims.append(im.copy().convert("RGBA"))
                if s[0] <= 32:
                    zoom.append(ims[-1].resize((s[0] * 6, s[1] * 6), Image.Resampling.NEAREST))
        else:
            ims.append(im.convert("RGBA"))
    rows, row, x = [], [], 0
    for i in ims:
        if row and x + i.width > 2600:
            rows.append(row)
            row, x = [], 0
        row.append(i)
        x += i.width + 10
    rows.append(row)
    rows.append(zoom)
    W = max(sum(i.width + 10 for i in r) for r in rows) + 10
    H = sum(max(i.height for i in r) + 10 for r in rows) + 10
    sheet = Image.new("RGBA", (W, H), (60, 64, 70, 255))
    y = 10
    for r in rows:
        x = 10
        for i in r:
            sheet.alpha_composite(i, (x, y))
            x += i.width + 10
        y += max(i.height for i in r) + 10
    sheet.save(path)


def write(d, files):
    for n, b in files.items():
        tmp = d / (n + ".tmp")
        tmp.write_bytes(b)
        os.replace(tmp, d / n)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--fonts", type=pathlib.Path, default=FONT_DIR)
    ap.add_argument("--fetch-fonts", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--preview", type=pathlib.Path)
    a = ap.parse_args(argv)
    if a.fetch_fonts:
        fetch_fonts(a.fonts)
        return 0
    if a.check:
        problems = check(a.fonts)
        for p in problems:
            print("  " + p)
        print("installer_art: " + (f"{len(problems)} problem(s)" if problems else "ok"))
        return 1 if problems else 0
    if not fonts_ok(a.fonts):
        print(f"installer_art: no pinned fonts in {a.fonts}: run with --fetch-fonts first", file=sys.stderr)
        return 1
    problems = palette_problems()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    fonts = Fonts(a.fonts)
    files, public = render(fonts), render_public(fonts)
    too_big = [f"{n}: {len(b)} bytes, the limit is under {limit(n)}" for n, b in public.items()
               if limit(n) and len(b) >= limit(n)]
    if too_big:
        print("\n".join(too_big), file=sys.stderr)
        return 1
    write(ART, files)
    write(PUBLIC, public)
    (ART / "art.json").write_text(json.dumps(manifest(files, public), indent=1) + "\n")
    if a.preview:
        preview(files, public, a.preview)
    print(f"installer_art: {len(files)} files in installer/art, {len(public)} in assets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
