#!/usr/bin/env python3
"""The setup program's artwork: the wizard's side image and header image at Inno Setup's four DPI scales, and the
setup's and the uninstaller's icons.

    tools/release/installer_art.py --fetch-fonts   # Fira Sans Condensed (SIL OFL 1.1) into --fonts, from Google Fonts'
                                                   # own repository; refuses a file whose SHA-256 is not FONTS'
    tools/release/installer_art.py                 # render installer/art/* and art.json (needs the fonts and Pillow)
    tools/release/installer_art.py --check         # exit 1 when an image is missing, changed or stale
    tools/release/installer_art.py --preview F.png # also write a contact sheet of every image (not shipped)

After Project Pyrrhus's and Project Helghast's tools/release/installer_art.py (undecuple, MIT): the image set, the
sizes, art.json and --check are theirs. The drawing is ours, from this project's own art (assets/banner.svg and
assets/icon.svg: the red eyes in the haze over a jungle ridge, the HUD corners) in the site's palette
(site/style.css), with the text in Fira Sans Condensed, as the banner. No game logo, symbol or game art. The fonts are
not committed or shipped, only these images.

Outputs (installer/art/): wizard-{100,125,150,200}.bmp (the Welcome and Finish pages' side image, 24-bit),
header-{100,125,150,200}.bmp (the small image at the top right of the other pages), vekta-setup.ico and
vekta-uninstall.ico (vekta.ico, the launcher's, with a badge: an arrow into a tray, and a red bar; 16 to 256 px), and
art.json (the SHA-256 of every file, of this script and of the fonts, and the Pillow version).
--check: with the fonts there it renders again and compares the bytes; without them it checks the files against
art.json and that art.json was made by this exact script (an edited script = stale art). The palette must still be in
site/style.css. Deterministic: the same script, fonts and Pillow give the same bytes.
Sizes: Inno Setup 6's own for 100/125/150/200 % (its help, WizardImageFile and WizardSmallImageFile); Inno picks the
closest one for the screen's DPI.
"""
import argparse
import hashlib
import io
import json
import os
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
ART = ROOT / "installer" / "art"
FONT_DIR = ROOT / "work" / "fonts"
FONT_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/firasanscondensed/"
FONTS = {
    "FiraSansCondensed-Bold.ttf": "b9371052e58ec5b709153dd0fffc60b5205b4d3435cf957c2522705b31b0dbcc",
    "FiraSansCondensed-Regular.ttf": "c67609f14fc9d328442bfffcc31988b0f76e5575cf460fcc1f3dd8e4912fe257",
}
SCALES = (100, 125, 150, 200)
WIZARD = {100: (164, 314), 125: (192, 386), 150: (246, 459), 200: (328, 604)}
HEADER = {100: (55, 55), 125: (64, 68), 150: (83, 80), 200: (110, 106)}
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
SITE = "gustavomorinaga.github.io/project-vekta"

# site/style.css :root
PAL = {"bg": "#0a0d0b", "panel": "#121915", "line": "#2a3a30", "red": "#d63a2a", "red-hi": "#ff6a4d",
       "steel": "#7fa3b8", "steel-hi": "#c4dbe8", "olive": "#8a9a5b", "text": "#e3e8e2", "dim": "#9aa79c"}


def rgb(name, a=255):
    h = PAL[name]
    return (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16), a)


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def fetch_fonts(d):
    d.mkdir(parents=True, exist_ok=True)
    for name, want in list(FONTS.items()) + [("OFL.txt", None)]:
        with urllib.request.urlopen(FONT_URL + name, timeout=60) as r:
            data = r.read()
        got = hashlib.sha256(data).hexdigest()
        if want and got != want:
            raise SystemExit(f"installer_art: {name}: SHA-256 {got}, not the pinned {want}")
        (d / name).write_bytes(data)
        print(f"fetched {name}")


def fonts_ok(d):
    return all((d / n).is_file() and sha(d / n) == h for n, h in FONTS.items())


# ------------------------------------------------------------------------------------------------------- drawing
SS = 3   # supersampling: drawn at 3x, then reduced (smooth edges at every size)


def backdrop(w, h, eyes_y, ridge_y, glow_r):
    """The banner's backdrop at w x h (already supersampled): the dark fade, the grid, the red haze around the eyes,
    the ridge and the eyes."""
    from PIL import Image, ImageDraw, ImageFilter
    im = Image.new("RGBA", (w, h), rgb("bg"))
    top = (0x16, 0x20, 0x1a)
    px = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(round(top[i] + (rgb("bg")[i] - top[i]) * t) for i in range(3))
        px.line([(0, y), (w, y)], fill=c + (255,))
    grid = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    g = ImageDraw.Draw(grid)
    step = max(6, round(w / 8))
    for x in range(0, w, step):
        g.line([(x, 0), (x, h)], fill=rgb("line", 70), width=max(1, SS // 2))
    for y in range(0, h, step):
        g.line([(0, y), (w, y)], fill=rgb("line", 70), width=max(1, SS // 2))
    im = Image.alpha_composite(im, grid)
    cx = w / 2
    haze = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    hd = ImageDraw.Draw(haze)
    hd.ellipse([cx - glow_r, eyes_y - glow_r * 0.8, cx + glow_r, eyes_y + glow_r * 0.8], fill=(0x3a, 0x14, 0x10, 200))
    im = Image.alpha_composite(im, haze.filter(ImageFilter.GaussianBlur(glow_r * 0.45)))
    # the ridge: a jungle treeline, the same jagged shape at every size (fractions of the width)
    pts = [(0, 0.55), (0.10, 0.35), (0.16, 0.50), (0.27, 0.05), (0.33, 0.42), (0.47, 0.15), (0.55, 0.0), (0.60, 0.38),
           (0.72, 0.30), (0.80, 0.08), (0.86, 0.36), (1.0, 0.25)]
    rh = h * 0.06
    poly = [(x * w, ridge_y + y * rh) for x, y in pts] + [(w, h), (0, h)]
    ImageDraw.Draw(im).polygon(poly, fill=(0x0c, 0x10, 0x0d, 255))
    eye_r = max(2 * SS, w * 0.022)
    gap = w * 0.085
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for ex in (cx - gap, cx + gap):
        gd.ellipse([ex - eye_r * 3, eyes_y - eye_r * 3, ex + eye_r * 3, eyes_y + eye_r * 3], fill=rgb("red", 150))
    im = Image.alpha_composite(im, glow.filter(ImageFilter.GaussianBlur(eye_r * 1.6)))
    d = ImageDraw.Draw(im)
    for ex in (cx - gap, cx + gap):
        d.ellipse([ex - eye_r, eyes_y - eye_r, ex + eye_r, eyes_y + eye_r], fill=rgb("red-hi"))
        d.ellipse([ex - eye_r * 0.45, eyes_y - eye_r * 0.45, ex + eye_r * 0.45, eyes_y + eye_r * 0.45],
                  fill=(0xff, 0xc8, 0xb8, 255))
    return im


def corners(d, box, arm, width, colour):
    x0, y0, x1, y1 = box
    d.line([(x0, y0 + arm), (x0, y0), (x0 + arm, y0)], fill=colour, width=width, joint="curve")
    d.line([(x1 - arm, y1), (x1, y1), (x1, y1 - arm)], fill=colour, width=width, joint="curve")


def fit(font_path, text, width, start):
    from PIL import ImageFont
    size = start
    while size > 6:
        f = ImageFont.truetype(str(font_path), size)
        if f.getlength(text) <= width:
            return f
        size -= 1
    return ImageFont.truetype(str(font_path), size)


def spaced(d, xy, text, font, fill, tracking):
    """Text with letter spacing (the site's letter-spacing .06em on headings)."""
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=font, fill=fill)
        x += font.getlength(ch) + tracking
    return x


def spaced_len(text, font, tracking):
    return sum(font.getlength(c) for c in text) + tracking * (len(text) - 1)


def fit_spaced(font_path, text, width, start, em):
    from PIL import ImageFont
    size = start
    while size > 6:
        f = ImageFont.truetype(str(font_path), size)
        if spaced_len(text, f, size * em) <= width:
            return f
        size -= 1
    return ImageFont.truetype(str(font_path), size)


def wizard(w, h, fonts):
    """The side image of the Welcome and Finish pages."""
    from PIL import Image, ImageDraw
    W, H = w * SS, h * SS
    u = W / 164                       # one unit at 100 %
    im = backdrop(W, H, eyes_y=H * 0.15, ridge_y=H * 0.27, glow_r=W * 0.42)
    d = ImageDraw.Draw(im)
    m = 12 * u
    corners(d, (6 * u, 6 * u, W - 6 * u, H - 6 * u), 18 * u, max(SS, round(1.6 * u)), rgb("steel"))
    bold, reg = fonts / "FiraSansCondensed-Bold.ttf", fonts / "FiraSansCondensed-Regular.ttf"
    y = H * 0.355
    f = fit_spaced(bold, "// UNOFFICIAL PC PORT", W - 2 * m, round(11 * u), 0.12)
    spaced(d, (m, y), "// UNOFFICIAL PC PORT", f, rgb("olive"), f.size * 0.12)
    y += f.size * 1.5
    for word in ("PROJECT", "VEKTA"):
        f = fit_spaced(bold, word, W - 2 * m, round(46 * u), 0.04)
        top = f.getbbox(word)[1]
        spaced(d, (m, y - top), word, f, rgb("text"), f.size * 0.04)
        y += f.size * 0.86
    y += 6 * u
    d.rectangle([m, y, m + 60 * u, y + max(SS, 1.6 * u)], fill=rgb("steel"))
    d.rectangle([m, y + 4 * u, m + 40 * u, y + 4 * u + max(SS, u)], fill=rgb("line"))
    y += 12 * u
    f = fit_spaced(bold, "KILLZONE HD (PS3)", W - 2 * m, round(13 * u), 0.08)
    spaced(d, (m, y), "KILLZONE HD (PS3)", f, rgb("steel-hi"), f.size * 0.08)
    y += f.size * 1.7
    f2 = None
    for line in ("BUILT ON YOUR PC", "FROM YOUR OWN GAME"):
        f2 = f2 or fit_spaced(reg, "FROM YOUR OWN GAME", W - 2 * m, round(11 * u), 0.08)
        spaced(d, (m, y), line, f2, rgb("dim"), f2.size * 0.08)
        y += f2.size * 1.35
    f = fit(reg, SITE, W - 2 * m - 16 * u, round(9 * u))     # clear of the corner at the bottom right
    d.text((m, H - 14 * u - f.size), SITE, font=f, fill=rgb("steel"))
    return im.resize((w, h), Image.LANCZOS).convert("RGB")


def header(w, h):
    """The small image at the top right: the icon's eyes and corners on the panel colour, no text."""
    from PIL import Image, ImageDraw
    W, H = w * SS, h * SS
    im = backdrop(W, H, eyes_y=H * 0.42, ridge_y=H * 0.62, glow_r=W * 0.45)
    d = ImageDraw.Draw(im)
    u = W / 55
    corners(d, (4 * u, 4 * u, W - 4 * u, H - 4 * u), 12 * u, max(SS, round(2 * u)), rgb("steel"))
    return im.resize((w, h), Image.LANCZOS).convert("RGB")


def badge(icon, kind):
    """vekta.ico's 256 px image with a badge at the bottom right: 'setup' an arrow into a tray (steel), 'uninstall' a
    bar (red)."""
    from PIL import Image, ImageDraw
    W = 256 * SS
    im = icon.resize((W, W), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    r = W * 0.24
    cx = cy = W - r - W * 0.03
    fill = rgb("steel") if kind == "setup" else rgb("red")
    d.ellipse([cx - r - W * 0.025, cy - r - W * 0.025, cx + r + W * 0.025, cy + r + W * 0.025], fill=rgb("bg"))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
    ink = rgb("bg")
    t = r * 0.2
    if kind == "setup":
        d.rectangle([cx - t / 2, cy - r * 0.62, cx + t / 2, cy + r * 0.05], fill=ink)
        d.polygon([(cx - r * 0.42, cy - r * 0.08), (cx + r * 0.42, cy - r * 0.08), (cx, cy + r * 0.36)], fill=ink)
        d.rectangle([cx - r * 0.55, cy + r * 0.44, cx + r * 0.55, cy + r * 0.44 + t], fill=ink)
    else:
        d.rectangle([cx - r * 0.58, cy - t * 0.75, cx + r * 0.58, cy + t * 0.75], fill=(0xf2, 0xf4, 0xef, 255))
    return im.resize((256, 256), Image.LANCZOS)


def ico_bytes(img):
    from PIL import Image
    big = img.convert("RGBA")
    frames = [big.resize((s, s), Image.LANCZOS) for s in ICON_SIZES]
    out = io.BytesIO()
    frames[-1].save(out, format="ICO", sizes=[(s, s) for s in ICON_SIZES], append_images=frames[:-1])
    return out.getvalue()


def launcher_icon():
    from PIL import Image
    im = Image.open(ART / "vekta.ico")
    im.size = (256, 256)
    im.load()
    return im.convert("RGBA")


def render(fonts, out):
    """{file name: bytes} of every image."""
    files = {}
    for s in SCALES:
        for name, img in ((f"wizard-{s}.bmp", wizard(*WIZARD[s], fonts)), (f"header-{s}.bmp", header(*HEADER[s]))):
            b = io.BytesIO()
            img.save(b, format="BMP")
            files[name] = b.getvalue()
    icon = launcher_icon()
    for kind in ("setup", "uninstall"):
        files[f"vekta-{kind}.ico"] = ico_bytes(badge(icon, kind))
    return files


def record(files, fonts):
    import PIL
    return {"about": "tools/release/installer_art.py's outputs; --check compares against this file",
            "script": sha(__file__), "pillow": PIL.__version__,
            "fonts": {n: sha(fonts / n) for n in FONTS}, "launcher_icon": sha(ART / "vekta.ico"),
            "files": {n: hashlib.sha256(b).hexdigest() for n, b in sorted(files.items())}}


def palette_problems():
    css = (ROOT / "site" / "style.css").read_text()
    return [f"site/style.css no longer has --{k}: {v}" for k, v in PAL.items() if f"--{k}: {v}" not in css]


def check(fonts):
    problems = palette_problems()
    try:
        art = json.loads((ART / "art.json").read_text())
    except (OSError, ValueError) as err:
        return [f"no art.json: {err}"]
    if art.get("script") != sha(__file__):
        problems.append("art.json was made by another version of this script: render again")
    if art.get("launcher_icon") != sha(ART / "vekta.ico"):
        problems.append("vekta.ico changed since the art was made: render again")
    for n, h in art.get("files", {}).items():
        p = ART / n
        if not p.is_file():
            problems.append(f"missing: installer/art/{n}")
        elif sha(p) != h:
            problems.append(f"changed: installer/art/{n}")
    if fonts_ok(fonts) and not problems:
        import PIL
        if PIL.__version__ == art.get("pillow"):
            for n, b in render(fonts, None).items():
                if hashlib.sha256(b).hexdigest() != art["files"].get(n):
                    problems.append(f"renders differently: {n}")
    return problems


def preview(files, path):
    from PIL import Image
    ims = [Image.open(io.BytesIO(b)) for n, b in sorted(files.items()) if n.endswith(".bmp")]
    for n in ("vekta-setup.ico", "vekta-uninstall.ico"):
        im = Image.open(io.BytesIO(files[n]))
        im.size = (256, 256)
        im.load()
        ims.append(im.convert("RGB"))
    W = sum(i.width for i in ims) + 10 * (len(ims) + 1)
    H = max(i.height for i in ims) + 20
    sheet = Image.new("RGB", (W, H), (40, 40, 40))
    x = 10
    for i in ims:
        sheet.paste(i, (x, 10))
        x += i.width + 10
    sheet.save(path)


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
    files = render(a.fonts, ART)
    for n, b in files.items():
        tmp = ART / (n + ".tmp")
        tmp.write_bytes(b)
        os.replace(tmp, ART / n)
    (ART / "art.json").write_text(json.dumps(record(files, a.fonts), indent=1) + "\n")
    if a.preview:
        preview(files, a.preview)
    print(f"installer_art: {len(files)} files in {ART}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
