#!/usr/bin/env python3
"""Website images: the PNGs the site shows -> light WebP copies in site/img/, and site/img/images.json.

The screenshots in docs/screenshots/ are full-size PNGs (1 to 3 MB each); the gallery and the viewer show these
copies instead and link to the PNG:
  site/img/shots/<name>-480.webp, -960.webp   thumbnails (quality 80), picked by the browser from srcset/sizes
  site/img/shots/<name>-1920.webp             the viewer's image (quality 88; at most 1920 wide, never enlarged)
  site/img/banner.webp, banner@2x.webp        assets/banner.png and banner@2x.png, lossless
  site/img/icon-180.png, icon-192.png, -256   assets/icon.png for the tab, the home screen and the manifest
                                              (the 180 px touch icon sits on the dark page colour: iOS shows
                                              transparency as black)
Which screenshots: every one docs/SCREENSHOTS.md lists (tools/website.py shots_found). The PNGs stay the originals;
nothing here changes them.

Deterministic for one Pillow and libwebp version. As in press_badges.py, images.json records both versions, this
script's SHA-256, each source's SHA-256 and each copy's SHA-256 and size. --check needs no Pillow: it compares the
files with images.json (a changed or new screenshot, a missing or extra copy, a changed script) and, only with the
same Pillow and libwebp, encodes every image again and compares the bytes. The project pins Pillow 12.3.0
(installer/art/art.json).

    python3 tools/release/site_images.py            # write site/img/ and images.json (needs Pillow with WebP)
    python3 tools/release/site_images.py --check    # exit 1 when site/img/ is not what this script writes
    python3 tools/release/site_images.py --root DIR # another repository root (the host test's copies)
"""
import argparse
import hashlib
import importlib.util
import io
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = "site/img"
MANIFEST = "site/img/images.json"
THUMBS = (480, 960)       # srcset widths of a gallery tile
FULL = 1920               # the viewer's image: at most this wide
Q_THUMB, Q_FULL = 80, 88
BANNERS = {"banner.webp": "assets/banner.png", "banner@2x.webp": "assets/banner@2x.png"}
ICON = "assets/icon.png"
ICONS = {"icon-180.png": 180, "icon-192.png": 192, "icon-256.png": 256}
TOUCH_BG = (0x0a, 0x0d, 0x10)    # site --bg (dark), under the touch icon

_spec = importlib.util.spec_from_file_location("website", ROOT / "tools" / "website.py")
website = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(website)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def versions():
    """{"pillow", "webp"}, or None when Pillow (with WebP) is not installed."""
    try:
        import PIL
        from PIL import features
    except ImportError:
        return None
    return {"pillow": PIL.__version__, "webp": features.version("webp") or "-"}


def stem(src):
    return pathlib.PurePosixPath(src).stem


def plan(root, problems):
    """[(out path under site/img, source path, kind, width or None)] for every copy this script writes."""
    out = []
    for _day, src, _title, _desc, (w, _h) in website.shots_found(root, problems):
        src = src.removeprefix("../")
        for t in THUMBS:
            out.append((f"shots/{stem(src)}-{t}.webp", src, "thumb", min(t, w)))
        out.append((f"shots/{stem(src)}-{FULL}.webp", src, "full", min(FULL, w)))
    for name, src in BANNERS.items():
        out.append((name, src, "lossless", None))
    for name, size in ICONS.items():
        out.append((name, ICON, "png", size))
    return out


def encode(path, kind, width):
    """The WebP bytes and size of one copy."""
    from PIL import Image
    with Image.open(path) as im:
        im.load()
        if kind == "png":
            im = im.convert("RGBA")
            if im.width != width:
                im = im.resize((width, width), Image.Resampling.LANCZOS)
            if width == ICONS["icon-180.png"]:
                bg = Image.new("RGBA", im.size, TOUCH_BG + (255,))
                im = Image.alpha_composite(bg, im).convert("RGB")
            buf = io.BytesIO()
            im.save(buf, "PNG", optimize=True)
            return buf.getvalue(), im.size
        im = im.convert("RGBA" if im.mode in ("RGBA", "LA", "P") and kind == "lossless" else "RGB")
        if width and im.width > width:
            im = im.resize((width, round(im.height * width / im.width)), Image.Resampling.LANCZOS)
        buf = io.BytesIO()
        if kind == "lossless":
            im.save(buf, "WEBP", lossless=True, quality=100, method=6, exact=False)
        else:
            im.save(buf, "WEBP", quality=Q_THUMB if kind == "thumb" else Q_FULL, method=6)
        return buf.getvalue(), im.size


def script_sha():
    return sha(pathlib.Path(__file__).read_bytes())


def build(root):
    problems = []
    items = plan(root, problems)
    if problems:
        return problems
    v = versions()
    if not v or v["webp"] == "-":
        return ["site_images: needs Pillow with WebP support (pip install Pillow==12.3.0)"]
    out = root / OUT
    images = {}
    for name, src, kind, width in items:
        data, (w, h) = encode(root / src, kind, width)
        (out / name).parent.mkdir(parents=True, exist_ok=True)
        (out / name).write_bytes(data)
        images[name] = {"from": src, "from_sha256": sha((root / src).read_bytes()), "sha256": sha(data),
                        "width": w, "height": h, "bytes": len(data)}
    keep = set(images) | {pathlib.PurePosixPath(MANIFEST).name}
    for f in sorted(out.rglob("*")):
        if f.is_file() and f.relative_to(out).as_posix() not in keep:
            f.unlink()          # a copy of a screenshot SCREENSHOTS.md no longer lists
    manifest = {**v, "script": script_sha(), "images": images}
    (root / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return []


def load(root):
    try:
        return json.loads((root / MANIFEST).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def check(root, reencode=True):
    problems = []
    items = plan(root, problems)
    m = load(root)
    if m is None:
        return problems + [f"{MANIFEST} is missing or not JSON: run tools/release/site_images.py"]
    if m.get("script") != script_sha():
        problems.append(f"{MANIFEST} was written by another version of this script: run it again")
    images = m.get("images", {})
    want = {name for name, *_ in items}
    for name, src, _kind, _width in items:
        rec, f = images.get(name), root / OUT / name
        if not rec:
            problems.append(f"{OUT}/{name} ({src}) is not in {MANIFEST}: run tools/release/site_images.py")
            continue
        if rec.get("from") != src or not (root / src).is_file() or sha((root / src).read_bytes()) != rec.get("from_sha256"):
            problems.append(f"{src} changed since {OUT}/{name} was made: run tools/release/site_images.py")
        if not f.is_file() or sha(f.read_bytes()) != rec.get("sha256"):
            problems.append(f"{OUT}/{name} is missing or differs from {MANIFEST}")
    for name in sorted(set(images) - want):
        problems.append(f"{MANIFEST} lists {name}, which no screenshot or banner needs: run tools/release/site_images.py")
    if (root / OUT).is_dir():
        for f in sorted((root / OUT).rglob("*")):
            rel = f.relative_to(root / OUT).as_posix()
            if f.is_file() and rel not in want and rel != pathlib.PurePosixPath(MANIFEST).name:
                problems.append(f"{OUT}/{rel} is not made by this script")
    v = versions()
    if reencode and not problems and v and v == {k: m.get(k) for k in ("pillow", "webp")}:
        for name, src, kind, width in items:
            if sha(encode(root / src, kind, width)[0]) != images[name]["sha256"]:
                problems.append(f"{OUT}/{name} is not what this script encodes from {src}")
    elif reencode and not problems:
        print(f"site_images: note: Pillow/libwebp {v} differ from {MANIFEST}'s; byte compare skipped")
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--root", type=pathlib.Path, default=ROOT)
    a = ap.parse_args(argv)
    problems = check(a.root) if a.check else build(a.root)
    for p in problems:
        print(f"site_images: {p}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
