#!/usr/bin/env python3
"""Host test for tools/release/site_images.py: the committed site/img/ passes --check (which encodes every copy again
when Pillow and libwebp match images.json); on a copy, a changed screenshot, a missing or changed copy, a stray file
and a screenshot SCREENSHOTS.md adds without a copy are each reported. Needs no Pillow."""
import importlib.util
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("site_images", ROOT / "tools/release/site_images.py")
si = importlib.util.module_from_spec(spec)
spec.loader.exec_module(si)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


bad = si.check(ROOT)
check(not bad, f"site_images --check passes: {bad}")


def copy():
    tmp = pathlib.Path(tempfile.mkdtemp())
    for d in ("site/img", "docs/screenshots", "assets"):
        shutil.copytree(ROOT / d, tmp / d)
    shutil.copy(ROOT / "docs/SCREENSHOTS.md", tmp / "docs/SCREENSHOTS.md")
    return tmp


def expect(change, words, what):
    tmp = copy()
    try:
        change(tmp)
        got = si.check(tmp, reencode=False)
        check(any(w in p for p in got for w in words), f"{what} is reported: {got}")
    finally:
        shutil.rmtree(tmp)


def flip(path):
    data = path.read_bytes()
    path.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))


first = sorted((ROOT / "site/img/shots").iterdir())[0].name
png = next(r["from"] for k, r in si.load(ROOT)["images"].items() if k.startswith("shots/"))
expect(lambda t: flip(t / png), ["changed since"], "a changed screenshot")
expect(lambda t: (t / "site/img/shots" / first).unlink(), ["missing or differs"], "a missing copy")
expect(lambda t: flip(t / "site/img/shots" / first), ["missing or differs"], "a changed copy")
expect(lambda t: (t / "site/img/extra.webp").write_bytes(b"x"), ["not made by this script"], "a stray file")
expect(lambda t: (t / "site/img/images.json").unlink(), ["missing or not JSON"], "a missing images.json")


def new_shot(t):
    shutil.copy(t / png, t / "docs/screenshots/zz-new.png")
    md = t / "docs/SCREENSHOTS.md"
    md.write_text(md.read_text(encoding="utf-8").replace(f"]({png.removeprefix('docs/')})",
                  f"]({png.removeprefix('docs/')}) | ![New](screenshots/zz-new.png)", 1), encoding="utf-8")


expect(new_shot, ["zz-new"], "a new screenshot without a copy")

print("test_site_images:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
