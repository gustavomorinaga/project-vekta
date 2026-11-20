#!/usr/bin/env python3
"""Host test for tools/website.py: the committed site/index.html and site/plain.html are what their sources give; on
a copy of the sources,
the patch count follows port/patches/series, the screens follow docs/SCREENSHOTS.md (escaped) and the news follow
CHANGELOG.md; a missing screenshot, an unreadable ROADMAP estimate or requirements table is reported; --check fails on
a stale page; inline Markdown is escaped and its links become GitHub URLs; the lint refuses scripts, event handlers
and outside resources other than Google Fonts, and allows only the same-origin site.js; both pages hold the same
sections."""
import importlib.util
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "website.py"
spec = importlib.util.spec_from_file_location("website", TOOL)
website = importlib.util.module_from_spec(spec)
spec.loader.exec_module(website)
SOURCES = ("VERSION", "CHANGELOG.md", ".recomp.json", "port/patches/series", "docs/features.tsv", "docs/SCREENSHOTS.md",
           "docs/ROADMAP.md", "docs/requirements.md", "site/index.html", "site/plain.html", "site/style.css",
           "site/plain.css", "site/site.js")
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def run(*args):
    return subprocess.run([sys.executable, str(TOOL), *args], capture_output=True, text=True)


def edit(path, old, new):
    text = path.read_text(encoding="utf-8")
    check(old in text, f"the fixture has '{old}' in {path.name}")
    path.write_text(text.replace(old, new), encoding="utf-8")


r = run("--check")
check(r.returncode == 0, "the committed page is current: " + (r.stdout + r.stderr).strip())

problems = []
page = website.render(ROOT, problems)
check(not problems and not website.lint(page), f"the real sources render cleanly: {problems + website.lint(page)}")
plain = website.render_plain(ROOT, [])
check(not website.lint(plain) and "<script" not in plain and "fonts.googleapis" not in plain,
      "the plain page loads no script and no web fonts")
ids = [s[0] for s in website.sections(ROOT, [])[0]]
check(all(f'<section id="{i}" class="sec"' in page and f'<section id="{i}" class="sec"' in plain for i in ids),
      "both pages hold every section")
check(ids[:3] == ["top", "shots", "status"] and ids[-1] == "legal", f"the menu order: {ids}")

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    for rel in SOURCES:
        (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, tmp / rel)
    shutil.copytree(ROOT / "docs/screenshots", tmp / "docs/screenshots")
    p = []
    check(website.render(tmp, p) == page and not p, "a copy of the sources gives the same page")
    check(run("--check", "--root", str(tmp)).returncode == 0, "--check passes on the copy")

    n = website.recomp(tmp, [])[1]
    with open(tmp / "port/patches/series", "a") as f:
        f.write("9999-new.patch\n")
    check(f"{n + 1} patches" in website.render(tmp, []), "the patch count follows port/patches/series")
    r = run("--check", "--root", str(tmp))
    check(r.returncode == 1 and "stale" in r.stderr, "--check fails on a stale page")

    edit(tmp / "docs/SCREENSHOTS.md", "Main menu, with its background movie", "Main menu & <movie>")
    check("<b>Main menu &amp; &lt;movie&gt;</b>" in website.render(tmp, []), "a screen's title follows SCREENSHOTS.md")
    edit(tmp / "CHANGELOG.md", "## [Unreleased]", "## [Unreleased]\n\n## [9.9.9] - 2026-11-02 (pre-release)\n\n"
         "### Added\n- A new thing.")
    out = website.render(tmp, [])
    check("<h3>9.9.9</h3>" in out and "2 Nov 2026 · pre-release" in out, "a dated changelog entry becomes news")
    check("<h3>Unreleased</h3>" not in out, "an empty entry is not news")

    first = sorted((tmp / "docs/screenshots").iterdir())[0]
    first.unlink()
    p = []
    website.render(tmp, p)
    check(any(first.name in x for x in p), f"a missing screenshot is reported: {p}")
    edit(tmp / "docs/ROADMAP.md", "of the whole project**", "of everything**")
    edit(tmp / "docs/requirements.md", "| Minimum |", "| Least |")
    p = []
    website.render(tmp, p)
    check(any("ROADMAP" in x for x in p), "an unreadable ROADMAP estimate is reported")
    check(any("Minimum" in x for x in p), "a requirements table without its Minimum column is reported")

got = website.inline("a <b> & `x<y` [notes](dev/a.md#s) **bold** [web](https://x.org)", "docs/")
want = ('a &lt;b&gt; &amp; <code>x&lt;y</code> '
        f'<a href="{website.REPO}/blob/main/docs/dev/a.md#s">notes</a> <strong>bold</strong> '
        '<a href="https://x.org">web</a>')
check(got == want, f"inline Markdown: {got}")
check(website.inline("[up](../README.md)", "docs/") == f'<a href="{website.REPO}/blob/main/README.md">up</a>',
      "a link out of docs/ resolves from the repository root")

for bad, what in [('<script>x</script>', "an inline script"), ('<script src="other.js"></script>', "another script"),
                  ('<script src="https://x.org/site.js"></script>', "an outside script"),
                  ('<img src="a.png" onerror="x">', "an event handler"),
                  ('<img src="https://x.org/a.png">', "an outside image"),
                  ('<link rel="stylesheet" href="//x.org/a.css">', "an outside stylesheet")]:
    check(website.lint(bad), f"the lint refuses {what}")
check(not website.lint('<script src="site.js" defer></script>'
                       '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=X">'
                       '<link rel="canonical" href="https://x.org/"><a href="https://x.org">x</a>'),
      "the lint allows site.js, Google Fonts, links and the canonical URL")

print("test_website:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
