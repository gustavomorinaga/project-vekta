#!/usr/bin/env python3
"""Writes the project page, site/index.html, from the files that already hold its facts, so the page cannot drift
from them. Never edit site/index.html by hand: change its source, run this, commit both. site/style.css is
hand-written.

    python3 tools/website.py           write site/index.html
    python3 tools/website.py --check   exit 1 if the page is not what this would write, a source cannot be read, a
                                       screenshot is missing, or the page would load a script or an outside resource

Where each part comes from:
  Status, PC features  docs/features.tsv (tools/features.py): ps3 and port rows with a `site` name, pc rows with a
                       `site` line; the date is the table's `# checked` date
  At a glance          the ps3recomp version in .recomp.json and the patch count of port/patches/series
  Screens              the newest `## YYYY-MM-DD` section of docs/SCREENSHOTS.md: its images, alt texts and captions
  Latest changes       the newest section of CHANGELOG.md
  Roadmap              the estimate and the phase table under `## Progress` in docs/ROADMAP.md
The rest (the intro, the build commands, the footer) is in the templates below. The page loads no scripts and nothing
from outside the site; links to GitHub point at the files on main.
(After Project Pyrrhus's and Project Helghast's tools/release/website.py, by undecuple, MIT.)
"""
import html
import importlib.util
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "https://github.com/gustavomorinaga/project-vekta"
BLOB = REPO + "/blob/main/"
PAGE = "site/index.html"
CHANGES_SHOWN = 6   # newest CHANGELOG items on the page

_spec = importlib.util.spec_from_file_location("features", os.path.join(ROOT, "tools", "features.py"))
features = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(features)

TODO = ' class="todo"'
TAGS = {"works": "ok", "partial": "part", "not yet": "no", "n/a": "no"}


class SourceError(Exception):
    pass


def read(root, rel):
    try:
        with open(os.path.join(root, rel), encoding="utf-8") as f:
            return f.read()
    except OSError as e:
        raise SourceError(f"{rel}: {e.strerror}") from None


def esc(s):
    """Text for the page: &, <, > and " escaped (an apostrophe stays as it is)."""
    return html.escape(s, quote=False).replace('"', "&quot;")


def gh_link(target, base):
    """A link in a Markdown file under `base` (a repository folder) as the URL the page uses."""
    if re.match(r"[a-z]+:", target) or target.startswith("#"):
        return target
    path, _, frag = target.partition("#")
    path = os.path.normpath(os.path.join(base, path)).replace(os.sep, "/")
    return BLOB + path + ("#" + frag if frag else "")


def inline(md, base=""):
    """Inline Markdown (code, links, bold) as HTML; everything else is escaped."""
    out, pos = [], 0
    for m in re.finditer(r"`([^`]+)`|\[([^\]]+)\]\(([^)\s]+)\)|\*\*([^*]+)\*\*", md):
        out.append(html.escape(md[pos:m.start()], quote=False))
        if m.group(1) is not None:
            out.append(f"<code>{html.escape(m.group(1), quote=False)}</code>")
        elif m.group(2) is not None:
            out.append(f'<a href="{esc(gh_link(m.group(3), base))}">{inline(m.group(2), base)}</a>')
        else:
            out.append(f"<strong>{inline(m.group(4), base)}</strong>")
        pos = m.end()
    out.append(html.escape(md[pos:], quote=False))
    return "".join(out)


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        raise SourceError(f"{os.path.relpath(path)}: not a PNG")
    return struct.unpack(">II", head[16:24])


def status_rows(rows):
    out = []
    for r in rows:
        if r["kind"] in ("ps3", "port") and r["site"]:
            # the note: the detail when it is a sentence of its own (": ..."), else the evidence
            note = r["detail"][1:].strip() if r["detail"].startswith(":") else r["evidence"]
            out.append((r["site"], r["status"], note))
    if not out:
        raise SourceError("docs/features.tsv: no ps3 or port row has a site name, so the status table would be empty")
    return out


def screens(root):
    text = read(root, "docs/SCREENSHOTS.md")
    m = re.search(r"^## (\d{4}-\d{2}-\d{2})\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m:
        raise SourceError("docs/SCREENSHOTS.md: no '## YYYY-MM-DD' section")
    date, body = m.groups()
    table = [[c.strip() for c in line.strip().strip("|").split("|")] for line in body.splitlines()
             if line.startswith("|") and not re.fullmatch(r"[|\s-]+", line)]
    shots = []
    for i, cells in enumerate(table):
        imgs = [re.fullmatch(r"!\[([^\]]+)\]\(([^)]+)\)", c) for c in cells]
        if not any(imgs):
            continue
        caps = table[i + 1] if i + 1 < len(table) else []
        for j, im in enumerate(imgs):
            if im:
                cap = caps[j] if j < len(caps) and not caps[j].startswith("![") else ""
                if not cap:
                    raise SourceError(f"docs/SCREENSHOTS.md: {im.group(2)} has no caption under it")
                src = os.path.normpath(os.path.join("docs", im.group(2))).replace(os.sep, "/")
                if not os.path.isfile(os.path.join(root, src)):
                    raise SourceError(f"docs/SCREENSHOTS.md: {src} does not exist")
                shots.append((src, im.group(1), cap, png_size(os.path.join(root, src))))
    if not shots:
        raise SourceError(f"docs/SCREENSHOTS.md: the {date} section has no images")
    return date, shots


def latest_changes(root):
    text = read(root, "CHANGELOG.md")
    m = re.search(r"^## ([^\n]+)\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m:
        raise SourceError("CHANGELOG.md: no '## ' section")
    items = [" ".join(it.split()) for it in re.findall(r"^- (.*?)(?=^- |^\S|\Z)", m.group(2), re.M | re.S)]
    if not items:
        raise SourceError(f"CHANGELOG.md: the '{m.group(1)}' section has no items")
    return m.group(1), items


def roadmap(root):
    text = read(root, "docs/ROADMAP.md")
    m = re.search(r"^## Progress\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m:
        raise SourceError("docs/ROADMAP.md: no '## Progress' section")
    body = m.group(1)
    est = re.search(r"\*\*about (\d+) % of the whole project\*\* and \*\*about (\d+) % of Killzone HD\*\*", body)
    phases = re.findall(r"^\| (\d+)\. ([^|]+?) \| \d+ \| (\d+) % \|$", body, re.M)
    if not est or not phases:
        raise SourceError("docs/ROADMAP.md: the Progress section's estimate or phase table cannot be read")
    return est.groups(), [(int(n), name, int(pct)) for n, name, pct in phases]


def recomp(root):
    m = re.search(r"ps3recomp (v\d+\.\d+\.\d+)", read(root, ".recomp.json"))
    if not m:
        raise SourceError(".recomp.json: no ps3recomp version")
    series = read(root, "port/patches/series")
    return m.group(1), sum(1 for line in series.splitlines() if line.strip() and not line.startswith("#"))


def render(root):
    checked, rows, problems = features.load()
    if problems:
        raise SourceError("docs/features.tsv: " + "; ".join(problems))
    status = status_rows(rows)
    pc = [r["site"] for r in rows if r["kind"] == "pc" and r["site"]]
    shot_date, shots = screens(root)
    change_head, changes = latest_changes(root)
    (all_pct, kz_pct), phases = roadmap(root)
    version, patches = recomp(root)
    e = esc

    status_html = "".join(
        f'        <tr><td>{e(name)}</td><td><span class="tag {TAGS[st]}">{e(st)}</span></td>'
        f"<td>{inline(note, 'docs')}</td></tr>\n" for name, st, note in status)
    pc_html = "".join(f"        <li>{e(line)}</li>\n" for line in pc)
    shots_html = "".join(
        f'      <figure><img src="../{e(src)}" width="{w}" height="{h}" loading="lazy" alt="{e(alt)}">'
        f"<figcaption>{e(cap)}</figcaption></figure>\n" for src, alt, cap, (w, h) in shots)
    changes_html = "".join(f"        <li>{inline(it)}</li>\n" for it in changes[:CHANGES_SHOWN])
    more = len(changes) - CHANGES_SHOWN
    more_html = f"{more} more, and every release: " if more > 0 else "Every release: "
    phases_html = "".join(
        f'        <li{"" if pct == 100 else TODO}>{n}. {e(name)} <span class="pct">{pct} %</span></li>\n'
        for n, name, pct in phases)
    return TEMPLATE.format(
        checked=checked, status=status_html, version=version, patches=patches, pc=pc_html, shot_date=shot_date,
        shots=shots_html, change_head=e(change_head), changes=changes_html, more=more_html, all_pct=all_pct,
        kz_pct=kz_pct, phases=phases_html, blob=BLOB, repo=REPO)


def lint(page):
    """Problems with what the page would load: no scripts or handlers, nothing from outside the site."""
    out = []
    if re.search(r"<script|<iframe|<object|<embed", page, re.I):
        out.append("the page has a script or an embed")
    if re.search(r"\son[a-z]+\s*=", page, re.I):
        out.append("the page has an event handler attribute")
    for tag in re.findall(r"<(?:img|link|source)\b[^>]*>", page, re.I):
        rel = re.search(r'\brel="([^"]*)"', tag)
        if rel and rel.group(1) == "canonical":
            continue
        for url in re.findall(r'\b(?:src|href|srcset)="([^"]*)"', tag):
            if re.match(r"[a-z]+:|//", url.split()[0]):
                out.append(f"the page loads {url} from outside the site")
    return out


def main():
    check = sys.argv[1:] == ["--check"]
    if sys.argv[1:] not in ([], ["--check"]):
        print(__doc__)
        return 2
    try:
        page = render(ROOT)
    except SourceError as err:
        print("website:", err)
        return 1
    problems = lint(page)
    for p in problems:
        print("website:", p)
    if problems:
        return 1
    path = os.path.join(ROOT, PAGE)
    if check:
        try:
            current = open(path, encoding="utf-8").read()
        except OSError:
            current = None
        if current != page:
            print(f"website: {PAGE} is not what its sources give; run python3 tools/website.py")
            return 1
        print(f"website: {PAGE} current")
        return 0
    with open(path, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"website: wrote {PAGE}")
    return 0


TEMPLATE = """\
<!doctype html>
<!-- Generated by tools/website.py. Do not edit by hand: change its sources and run it. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Project Vekta</title>
<meta name="description" content="Project Vekta: an unofficial PC port of Killzone HD (PS3) by static recompilation, built on your PC from your own game. Linux and Windows, Vulkan.">
<meta property="og:title" content="Project Vekta">
<meta property="og:description" content="Killzone HD's own PS3 code, translated to C++ and running natively on your PC. Built from your own game.">
<meta property="og:type" content="website">
<meta property="og:url" content="https://gustavomorinaga.github.io/project-vekta/site/">
<meta property="og:image" content="https://gustavomorinaga.github.io/project-vekta/assets/social-preview.png">
<meta property="og:image:alt" content="Project Vekta: Killzone HD, unofficial PC port by static recompilation">
<meta name="twitter:card" content="summary_large_image">
<link rel="canonical" href="https://gustavomorinaga.github.io/project-vekta/site/">
<link rel="stylesheet" href="style.css">
</head>
<body>
<header class="wrap">
  <img class="banner" src="../assets/banner.png" srcset="../assets/banner@2x.png 2x" width="1280" height="320"
       alt="Project Vekta. Killzone HD: unofficial PC port by static recompilation, built from your own game">
  <nav class="tabs" aria-label="Sections">
    <a href="#about">About</a>
    <a href="#status">Status</a>
    <a href="#shots">Screens</a>
    <a href="#features">PC features</a>
    <a href="#build">Build it</a>
    <a href="#changes">Changes</a>
    <a href="#roadmap">Roadmap</a>
    <a class="gh" href="{repo}">GitHub</a>
  </nav>
</header>

<main class="wrap">
  <section id="about" class="box">
    <p class="kicker">Unofficial PC port · work in progress</p>
    <h1>Killzone HD, native on PC</h1>
    <p class="lead">Project Vekta runs the PS3 remaster of the first Killzone as a native Linux or Windows program.
      The game's own PowerPC and Cell SPU code is translated to C++ ahead of time with
      <a href="https://github.com/sp00nznet/ps3recomp">ps3recomp</a> and compiled on your machine; the PS3's system
      software is reimplemented around it, and the graphics are replayed on Vulkan. It is not an emulator and not a
      rewrite.</p>
    <p><strong>You need your own copy of the game.</strong> This project contains only the port: code, patches,
      tools and documentation. No game code, data, assets, keys or firmware are included or downloaded.</p>
    <div class="btns">
      <a class="btn" href="{blob}docs/BUILDING.md">Build it</a>
      <a class="btn alt" href="{blob}docs/GETTING-FILES.md">Getting your files</a>
      <a class="btn alt" href="{blob}docs/KNOWN_ISSUES.md">Known issues</a>
    </div>
  </section>

  <div class="cols">
    <section id="status" class="box">
      <h2>Status</h2>
      <p class="dim">As of {checked}, Linux x64 with an RTX 3070 Ti (the maintainer's machine).</p>
      <div class="table-wrap">
      <table>
        <tr><th>Area</th><th>State</th><th>Notes</th></tr>
{status}      </table>
      </div>
      <p><a href="{blob}docs/FEATURES.md">Every feature of the PS3 game, with its evidence</a></p>
    </section>

    <aside class="box">
      <h2>At a glance</h2>
      <ul class="ticks">
        <li>Game: Killzone HD, PS3 (NPUA80856), from the Killzone Trilogy disc</li>
        <li>Recompiler: ps3recomp {version} plus {patches} patches</li>
        <li>Platforms: Linux x64 native, Windows x64 (cross-built)</li>
        <li>Graphics: Vulkan</li>
        <li>CPU: x86-64 with AVX2</li>
        <li>Online: nothing; the port makes no network connection</li>
        <li>Licence: MIT</li>
      </ul>
      <p class="dim"><a href="{blob}docs/requirements.md">Requirements</a></p>
    </aside>
  </div>

  <section id="shots" class="box">
    <h2>Screens</h2>
    <p class="dim">Captured from the port on {shot_date} (Linux, headless run).
      <a href="{blob}docs/SCREENSHOTS.md">All screenshots</a></p>
    <div class="gallery">
{shots}    </div>
  </section>

  <div class="cols">
    <section id="features" class="box">
      <h2>PC features</h2>
      <p>All optional, on a <strong>PC</strong> page inside the game's own Options and Pause menus:</p>
      <ul class="ticks">
{pc}      </ul>
      <p><a href="{blob}docs/FEATURES.md">The full list, with the PS3 parity table</a></p>
    </section>

    <aside id="build" class="box">
      <h2>Build it</h2>
      <p>On Linux, with your files in <code>Game/</code>:</p>
<pre>git clone --recurse-submodules \\
  {repo}
cd project-vekta
tools/check-game-files.py
tools/ps3recomp-patched.sh
tools/lift.sh
tools/setup-vfs.sh
tools/build.sh
tools/play.sh</pre>
      <p class="dim">Requirements and the Windows cross build:
        <a href="{blob}docs/BUILDING.md">BUILDING.md</a>.</p>
    </aside>
  </div>

  <div class="cols">
    <section id="changes" class="box">
      <h2>Latest changes</h2>
      <p class="dim">{change_head}</p>
      <ul class="ticks">
{changes}      </ul>
      <p>{more}<a href="{blob}CHANGELOG.md">CHANGELOG.md</a></p>
    </section>

    <aside id="roadmap" class="box">
      <h2>Roadmap</h2>
      <p>About <strong>{kz_pct} %</strong> of Killzone HD and <strong>{all_pct} %</strong> of the whole project
        (the three games).</p>
      <ul class="ticks">
{phases}      </ul>
      <p><a href="{blob}docs/ROADMAP.md">The roadmap, item by item</a></p>
    </aside>
  </div>
</main>

<footer>
  <div class="wrap">
    <div>
      <h3>Project Vekta</h3>
      <p>A fan project. Killzone and Killzone HD are (c) Sony Interactive Entertainment. Not affiliated with or endorsed
        by Sony Interactive Entertainment or Guerrilla Games.</p>
    </div>
    <div>
      <h3>Built on</h3>
      <p><a href="https://github.com/sp00nznet/ps3recomp">ps3recomp</a>, and the work of
        <a href="https://github.com/undecuple/project-helghast">Project Helghast</a> and
        <a href="https://github.com/undecuple/project-pyrrhus">Project Pyrrhus</a>.
        Credits: <a href="{blob}NOTICE.md">NOTICE.md</a>.</p>
    </div>
    <div>
      <h3>This page</h3>
      <p>No trackers, no cookies, no external fonts or scripts. Generated from the repository by
        <a href="{blob}tools/website.py">tools/website.py</a>. Port code MIT.</p>
    </div>
  </div>
</footer>
</body>
</html>
"""

if __name__ == "__main__":
    sys.exit(main())
