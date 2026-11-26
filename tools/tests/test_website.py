#!/usr/bin/env python3
"""Host test for tools/website.py: the committed site/index.html and site/plain.html are what their sources give; on
a copy of the sources,
the patch count follows port/patches/series, the screens follow docs/SCREENSHOTS.md (escaped) and the news follow
CHANGELOG.md; a missing screenshot, an unreadable ROADMAP estimate or requirements table is reported; --check fails on
a stale page; inline Markdown is escaped and its links become GitHub URLs; the lint refuses scripts, event handlers
and every outside resource (Google Fonts too), and allows only the same-origin site.js; both pages hold the same
sections; the gallery shows the WebP copies of site/img/images.json and reports a screenshot without one; the title
and description fit a search result, the FAQ follows docs/FAQ.md, the sitemap's date follows CHANGELOG.md and lists
the screenshots, the 404 page links absolutely and is not indexed, the lint allows only a JSON-LD data block that
parses."""
import importlib.util
import json
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
           "docs/ROADMAP.md", "docs/requirements.md", "docs/FAQ.md", "site/index.html", "site/plain.html",
           "site/404.html", "site/sitemap.xml", "site/manifest.webmanifest", "site/style.css", "site/plain.css",
           "site/site.js", "site/img/images.json")
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
check(not website.lint(plain) and "<script" not in plain and "woff2" not in plain and "fonts.googleapis" not in page,
      "the plain page loads no script and no web fonts")
ids = [s[0] for s in website.sections(ROOT, [])[0]]
check(all(f'<section id="{i}" class="sec"' in page and f'<section id="{i}" class="sec"' in plain for i in ids),
      "both pages hold every section")
check(ids[:3] == ["top", "shots", "status"] and ids[-1] == "legal" and "faq" in ids, f"the menu order: {ids}")
check(len(website.TITLE) <= 60 and 50 <= len(website.META_DESC) <= 155,
      f"title {len(website.TITLE)} and description {len(website.META_DESC)} characters fit a search result")
check(f"<title>{website.TITLE}</title>" in page.replace("&#x27;", "'") and '"@type":"SoftwareApplication"' in page,
      "the index carries the title and the JSON-LD")
check('<link rel="canonical" href="' + website.SITE_URL + '">' in plain, "the plain page names the index as canonical")
nf = website.render_404()
check('name="robots" content="noindex"' in nf and 'href="/project-vekta/"' in nf and not website.lint(nf),
      "the 404 page is not indexed and links from the site's root")

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
    edit(tmp / "CHANGELOG.md", "## [9.9.9]", "## [9.9.10] - 2026-12-31\n\n### Fixed\n- A later thing.\n\n## [9.9.9]")
    check("<lastmod>2026-12-31</lastmod>" in website.render_sitemap(tmp, []), "the sitemap's date is the newest entry's")
    edit(tmp / "docs/FAQ.md", "## Is this an emulator?", "## Is this an <emulator>?")
    check("<h3>Is this an &lt;emulator&gt;?</h3>" in website.render(tmp, []), "a question follows docs/FAQ.md (escaped)")
    sm = website.render_sitemap(tmp, [])
    check("-1920.webp</image:loc>" in sm and sm.count("<url>") == 1, "the sitemap lists the page and its screenshots")

    m = tmp / "site/img/images.json"
    data = json.loads(m.read_text(encoding="utf-8"))
    gone = next(k for k in data["images"] if k.endswith("-960.webp"))
    del data["images"][gone]
    m.write_text(json.dumps(data), encoding="utf-8")
    p = []
    out = website.render(tmp, p)
    check(any("960 wide WebP" in x for x in p), f"a screenshot without its WebP copy is reported: {p}")
    check('src="img/shots/' in out and 'data-full="../docs/screenshots/' in out,
          "a tile shows the WebP copy and links the PNG")
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
check(website.lint('<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=X">'),
      "the lint refuses Google Fonts")
check(website.lint('<script type="application/ld+json">{"a":</script>'), "the lint refuses JSON-LD that does not parse")
check(not website.lint('<script type="application/ld+json">{"a": 1}</script>'), "the lint allows a JSON-LD data block")
check(not website.lint('<script src="site.js" defer></script><link rel="preload" href="fonts/x.woff2" as="font">'
                       '<link rel="canonical" href="https://x.org/"><a href="https://x.org">x</a>'),
      "the lint allows site.js, the local font, links and the canonical URL")

print("test_website:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
