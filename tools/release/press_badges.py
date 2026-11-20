#!/usr/bin/env python3
"""Launcher badges: assets/press/ -> the "Get it on" badges the README, docs/INSTALL.md and the site show.

  assets/press/recomplauncher/  badge-dark.svg and badge-dark.png from RecompLauncher's press kit
                                (https://recomplauncher.com/press), byte for byte: the kit asks that its badge is used
                                as it is, never recoloured, stretched or redrawn, and linked to the port's page there.
                                Its README says badge-dark works on light and dark pages, so only that one is kept.
                                Their SHA-256 is pinned below (KIT); a changed file fails --check.
  assets/press/quiver/          badge.png (313x56) and badge@3x.png (939x168), drawn here: TEXT ONLY ("GET IT ON" /
                                "Quiver Launcher"), as tall as the RecompLauncher badge and as wide as its text.
                                Quiver Launcher has no press kit, and its TRADEMARKS.md allows saying truthfully that a
                                project works with it but not using its logo or app icons, or artwork that could be
                                mistaken for them, so no Quiver logo, icon or colour is used. The font is the project's
                                own (tools/release/installer_art.py FONTS, pinned by SHA-256).
  assets/press/press.json       the SHA-256 of every file above, written by this script.

Deterministic for one Pillow version: the same fonts give the same bytes. As in installer_art.py, press.json
records the Pillow version and this script's SHA-256; --check always compares the files with press.json, and draws
the badge again to compare only with that same Pillow version (another one may round differently).

    python3 tools/release/press_badges.py            # draw the Quiver badge, write press.json
    python3 tools/release/press_badges.py --check    # exit 1 when a kit file changed, the Quiver badge is not what
                                                     # this script draws, or press.json is stale (needs the fonts:
                                                     # installer_art.py --fetch-fonts)
"""
import argparse
import hashlib
import importlib.util
import io
import json
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
PRESS = ROOT / "assets" / "press"
MANIFEST = PRESS / "press.json"
RECOMPLAUNCHER_PAGE = "https://recomplauncher.com/game/project-vekta"
QUIVER_PAGE = "https://quiverlauncher.com/apps/killzone-hd-project-vekta"
# RecompLauncher press kit (recomplauncher-press-kit.zip, files dated 2026-10-27), checked 2026-11-20.
KIT = {
    "recomplauncher/badge-dark.svg": "ad29e778c2e8c6c6dbe95fa100914ea2e8d36346c0fa09308ec2a051c5c300a8",
    "recomplauncher/badge-dark.png": "21182acee84aa869a68923e4647b0ff4e4e2bf968004590c893a1003fe631600",
}
HEIGHT = 56             # the RecompLauncher badge's height, so the two line up; the width follows the text
PAD = 18                # left and right padding around the text, at 1x
SCALES = {"badge.png": 1, "badge@3x.png": 3}
# Neutral colours of our own: a near-black plate with a grey rim, white name, grey small line.
PLATE, RIM, SMALL, INK = "#05080d", "#3a4452", "#c7d0dc", "#ffffff"

_spec = importlib.util.spec_from_file_location("installer_art", pathlib.Path(__file__).with_name("installer_art.py"))
installer_art = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(installer_art)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def font_files(fonts):
    """(bold, regular) font files from installer_art's pinned set."""
    names = sorted(installer_art.FONTS)
    bold = next(n for n in names if "Bold" in n)
    other = next(n for n in names if n != bold)
    return fonts / bold, fonts / other


def badge_width(fonts):
    """The badge's width at 1x: the name's width plus PAD on each side, rounded up."""
    name = ImageFont.truetype(str(font_files(fonts)[0]), 25 * 4)
    return -(-int(name.getlength("Quiver Launcher")) // 4) + 2 * PAD


def quiver_badge(scale, fonts):
    """The text-only Quiver badge at scale x (badge_width x 56 at 1), as PNG bytes."""
    bold, regular = font_files(fonts)
    ss = 4   # drawn larger, then reduced: smooth edges
    k = scale * ss
    width = badge_width(fonts)
    w, h = width * k, HEIGHT * k
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((k // 2, k // 2, w - 1 - k // 2, h - 1 - k // 2), radius=12 * k, fill=PLATE, outline=RIM,
                        width=k)
    small = ImageFont.truetype(str(regular), 11 * k)
    name = ImageFont.truetype(str(bold), 25 * k)
    spacing = 1.6 * k   # the small line is letter-spaced
    name_w = d.textlength("Quiver Launcher", font=name)
    left = (w - name_w) / 2   # the block is centred: there is no icon at the left
    x = left + k
    for ch in "GET IT ON":
        d.text((x, 9 * k), ch, font=small, fill=SMALL)
        x += d.textlength(ch, font=small) + spacing
    d.text((left, 21 * k), "Quiver Launcher", font=name, fill=INK)
    im = im.resize((width * scale, HEIGHT * scale), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def manifest(drawn):
    files = dict(KIT)
    files.update({f"quiver/{n}": sha(b) for n, b in drawn.items()})
    import PIL
    return json.dumps({"script": sha(pathlib.Path(__file__).read_bytes()), "pillow": PIL.__version__,
                       "links": {"recomplauncher": RECOMPLAUNCHER_PAGE, "quiver": QUIVER_PAGE},
                       "fonts": {n: h for n, h in sorted(installer_art.FONTS.items())},
                       "files": dict(sorted(files.items()))}, indent=2) + "\n"


def problems(fonts):
    out = []
    for rel in KIT:
        p = PRESS / rel
        if not p.is_file():
            out.append(f"{rel} missing (copy it unchanged from RecompLauncher's press kit)")
        elif sha(p.read_bytes()) != KIT[rel]:
            out.append(f"{rel} is not the press kit's file (SHA-256 pinned in KIT): use it unchanged")
    if out:
        return out
    try:
        pinned = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, ValueError) as err:
        return [f"no press.json: {err} (run press_badges.py)"]
    if pinned.get("script") != sha(pathlib.Path(__file__).read_bytes()):
        out.append("press.json was made by another version of this script (run press_badges.py)")
    if pinned.get("files", {}) != {**KIT, **{f"quiver/{n}": pinned.get("files", {}).get(f"quiver/{n}") for n in SCALES}}:
        out.append("press.json does not list the kit files and the Quiver badges")
    for n in SCALES:
        p = PRESS / "quiver" / n
        if not p.is_file():
            out.append(f"quiver/{n} missing (run press_badges.py)")
        elif sha(p.read_bytes()) != pinned["files"].get(f"quiver/{n}"):
            out.append(f"quiver/{n} changed since press.json was written (run press_badges.py)")
    import PIL
    if not out and installer_art.fonts_ok(fonts) and PIL.__version__ == pinned.get("pillow"):
        for n, s in SCALES.items():
            if sha(quiver_badge(s, fonts)) != pinned["files"][f"quiver/{n}"]:
                out.append(f"quiver/{n} is not what this script draws (run press_badges.py)")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--fonts", type=pathlib.Path, default=installer_art.FONT_DIR)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    if a.check:
        bad = problems(a.fonts)
        for p in bad:
            print("press_badges:", p, file=sys.stderr)
        if not bad:
            print("press_badges: ok")
        return 1 if bad else 0
    missing = [rel for rel in KIT if not (PRESS / rel).is_file()]
    if missing or not installer_art.fonts_ok(a.fonts):
        print(f"press_badges: missing {missing or 'fonts (run installer_art.py --fetch-fonts)'}", file=sys.stderr)
        return 1
    drawn = {n: quiver_badge(s, a.fonts) for n, s in SCALES.items()}
    (PRESS / "quiver").mkdir(parents=True, exist_ok=True)
    for n, b in drawn.items():
        (PRESS / "quiver" / n).write_bytes(b)
    MANIFEST.write_text(manifest(drawn), encoding="utf-8")
    print(f"press_badges: wrote {len(drawn)} Quiver badges and {MANIFEST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
