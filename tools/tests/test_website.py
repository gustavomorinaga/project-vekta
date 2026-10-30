#!/usr/bin/env python3
"""Host test for tools/website.py: the committed site/index.html is what its sources give; a changed source makes the
page stale (the patch count follows port/patches/series, the screens follow docs/SCREENSHOTS.md); a missing
screenshot or an unreadable ROADMAP is refused; inline Markdown is escaped and its links become GitHub URLs; the lint
refuses scripts, event handlers and resources from outside the site."""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("website", os.path.join(ROOT, "tools", "website.py"))
website = importlib.util.module_from_spec(spec)
spec.loader.exec_module(website)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "website.py"), "--check"], capture_output=True, text=True)
check(r.returncode == 0, "the committed page is current: " + r.stdout.strip())

page = website.render(ROOT)
check(not website.lint(page), f"the page passes its own lint: {website.lint(page)}")

# A copy of the sources the page reads, changed one at a time.
with tempfile.TemporaryDirectory() as tmp:
    for rel in ("CHANGELOG.md", ".recomp.json", "port/patches/series", "docs/SCREENSHOTS.md", "docs/ROADMAP.md"):
        os.makedirs(os.path.dirname(os.path.join(tmp, rel)) or tmp, exist_ok=True)
        shutil.copy(os.path.join(ROOT, rel), os.path.join(tmp, rel))
    shutil.copytree(os.path.join(ROOT, "docs/screenshots"), os.path.join(tmp, "docs/screenshots"))
    check(website.render(tmp) == page, "a copy of the sources gives the same page")

    n = website.recomp(tmp)[1]
    with open(os.path.join(tmp, "port/patches/series"), "a") as f:
        f.write("9999-new.patch\n")
    check(f"plus {n + 1} patches" in website.render(tmp), "the patch count follows port/patches/series")

    shots = os.path.join(tmp, "docs/SCREENSHOTS.md")
    text = open(shots).read()
    with open(shots, "w") as f:
        f.write(text.replace("Main menu, with its background movie", "Main menu & <movie>"))
    check("<figcaption>Main menu &amp; &lt;movie&gt;</figcaption>" in website.render(tmp),
          "a caption follows docs/SCREENSHOTS.md, escaped")
    first = sorted(os.listdir(os.path.join(tmp, "docs/screenshots")))[0]
    os.unlink(os.path.join(tmp, "docs/screenshots", first))
    try:
        website.render(tmp)
        check(False, "a missing screenshot is refused")
    except website.SourceError as e:
        check(first in str(e), f"the refusal names the missing screenshot: {e}")

    with open(os.path.join(tmp, "docs/ROADMAP.md"), "w") as f:
        f.write("# Roadmap\n\n## Progress\n\nSoon.\n")
    try:
        website.roadmap(tmp)
        check(False, "an unreadable ROADMAP progress section is refused")
    except website.SourceError:
        pass

got = website.inline("a <b> & `x<y` [notes](dev/a.md#s) **bold** [web](https://x.org)", "docs")
want = ('a &lt;b&gt; &amp; <code>x&lt;y</code> '
        f'<a href="{website.BLOB}docs/dev/a.md#s">notes</a> <strong>bold</strong> <a href="https://x.org">web</a>')
check(got == want, f"inline Markdown: {got}")
check(website.inline("[up](../README.md)", "docs") == f'<a href="{website.BLOB}README.md">up</a>',
      "a link out of docs/ resolves from the repository root")

for bad, what in [('<script>x</script>', "a script"), ('<img src="a.png" onerror="x">', "an event handler"),
                  ('<img src="https://x.org/a.png">', "an outside image"),
                  ('<link rel="stylesheet" href="//x.org/a.css">', "an outside stylesheet")]:
    check(website.lint(bad), f"the lint refuses {what}")
check(not website.lint('<link rel="canonical" href="https://x.org/"><a href="https://x.org">x</a>'),
      "the lint allows links and the canonical URL")

print("test_website:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
