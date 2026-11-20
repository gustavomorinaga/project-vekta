#!/usr/bin/env python3
"""Host test for tools/release/check_site.py: the committed site passes; on a copy, each kind of fault is caught (a
broken fragment, a missing image, an outside script or stylesheet, an inline handler, a page without its policy, the
two pages drifting apart, a class without a rule, a network call or a lost feature in site.js)."""
import importlib.util
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("check_site", ROOT / "tools/release/check_site.py")
check_site = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_site)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


problems = check_site.check(ROOT / "site")
check(not problems, f"the committed site passes: {problems}")


def faulty(rel, old, new, what):
    """Copy site/, assets/ and docs/screenshots/, make one edit, and expect check_site to report it."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        shutil.copytree(ROOT / "site", tmp / "site")
        shutil.copytree(ROOT / "assets", tmp / "assets")
        shutil.copytree(ROOT / "docs/screenshots", tmp / "docs/screenshots")
        f = tmp / "site" / rel
        text = f.read_text(encoding="utf-8")
        check(old in text, f"the fixture has '{old}' in {rel}")
        f.write_text(text.replace(old, new), encoding="utf-8")
        check(check_site.check(tmp / "site"), f"check_site catches {what}")


faulty("index.html", 'href="#install"', 'href="#nowhere"', "a fragment with no target")
faulty("index.html", "../assets/banner.png", "../assets/missing.png", "a missing image")
faulty("index.html", '<script src="site.js" defer></script>', '<script src="https://x.org/a.js"></script>',
       "an outside script")
faulty("plain.html", '<link rel="stylesheet" href="plain.css">', '<link rel="stylesheet" href="https://x.org/a.css">',
       "an outside stylesheet")
faulty("index.html", '<main id="panel"', '<main onclick="x()" id="panel"', "an inline event handler")
faulty("index.html", "script-src 'self'; ", "", "the index without its script policy")
faulty("plain.html", "<h2 id=\"h-news\">News</h2>", "<h2 id=\"h-news\">Updates</h2>", "the pages' headings drifting")
faulty("plain.html", '<a href="https://github.com/undecuple/project-helghast">Project Helghast</a> (the Killzone 3',
       "Project Helghast (the Killzone 3", "the pages' links drifting")
faulty("index.html", 'class="lead"', 'class="lead nostyle"', "a class without a rule")
faulty("site.js", "'use strict';", "'use strict'; fetch('x');", "a network call in site.js")
faulty("site.js", "getGamepads", "getPads", "a lost game pad in site.js")
faulty("index.html", "</main>", "</div></main>", "a tag closed out of order")

print("test_site:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
