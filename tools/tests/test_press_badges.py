#!/usr/bin/env python3
"""Host test for tools/release/press_badges.py: the committed badges pass --check (which draws them again when the
pinned fonts are in work/fonts and Pillow is the version press.json names); the RecompLauncher files
are the press kit's (pinned SHA-256); a changed kit file is refused; the Quiver badge is text only, as tall as
the RecompLauncher badge; the README, docs/INSTALL.md and the site link both badges to the port's pages."""
import hashlib
import importlib.util
import pathlib
import shutil
import sys
import struct
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("press_badges", ROOT / "tools/release/press_badges.py")
press = importlib.util.module_from_spec(spec)
spec.loader.exec_module(press)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


for rel, want in press.KIT.items():
    got = hashlib.sha256((press.PRESS / rel).read_bytes()).hexdigest()
    check(got == want, f"{rel} is the press kit's file")

fonts = press.installer_art.FONT_DIR
bad = press.problems(fonts)
check(not bad, f"press_badges --check passes: {bad}")

def png_header(path):
    """(width, height, colour type) from a PNG's IHDR, without Pillow (CI's checks have none)."""
    data = path.read_bytes()[:26]
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return w, h, data[25]


for name, scale in press.SCALES.items():
    path = press.PRESS / "quiver" / name
    head = png_header(path)
    check(head and head[1] == press.HEIGHT * scale and head[0] < 313 * scale, f"quiver/{name} is {head}")
    check(head and head[2] == 6, f"quiver/{name} is RGBA")   # colour type 6: truecolour with alpha
    if press.pillow():
        from PIL import Image
        check(Image.open(path).getpixel((0, 0))[3] == 0, f"quiver/{name} has transparent corners")
quiver_files = sorted(p.name for p in (press.PRESS / "quiver").iterdir())
check(quiver_files == ["README.txt", "badge.png", "badge@3x.png"], f"only our text badge in quiver/: {quiver_files}")

with tempfile.TemporaryDirectory() as tmp:
    saved = press.PRESS
    press.PRESS = pathlib.Path(tmp) / "press"
    shutil.copytree(saved, press.PRESS)
    f = press.PRESS / "recomplauncher/badge-dark.svg"
    f.write_bytes(f.read_bytes() + b"<!-- changed -->")
    check(any("press kit" in p for p in press.problems(fonts)), "a changed kit badge is refused")
    press.PRESS = saved

for rel in ("README.md", "docs/INSTALL.md", "site/index.html"):
    text = (ROOT / rel).read_text(encoding="utf-8")
    check(press.RECOMPLAUNCHER_PAGE in text and "press/recomplauncher/badge-dark.svg" in text,
          f"{rel} links the RecompLauncher badge to the port's page")
    check(press.QUIVER_PAGE in text and "press/quiver/badge" in text, f"{rel} links the Quiver badge to the port's page")
    width = png_header(press.PRESS / "quiver" / "badge.png")[0]
    check(f'badge@3x.png" width="{width}" height="56"' in text or f'3x" width="{width}" height="56"' in text,
          f"{rel} gives the Quiver badge its width, {width}")

print("test_press_badges:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
