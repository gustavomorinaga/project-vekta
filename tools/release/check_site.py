#!/usr/bin/env python3
"""Checks of the written project site (site/: index.html, plain.html, style.css, plain.css, site.js), on the
repository layout (site/ next to assets/ and docs/screenshots/, as pages.yml stages it).

  1. both pages are well-formed: every tag closed in order, ids unique;
  2. every #fragment resolves on its page, every relative link and image resolves to a file, and an other-page link
     (plain.html#x) finds its fragment there;
  3. nothing loads from outside: no outside stylesheet, no script but site.js (index only), no inline script or event
     handler, no @import or outside url() in the CSS, no web font on the plain page;
  4. the two pages agree: the same sections in the same order, the same headings, the same links inside them;
  5. every class index.html uses has a selector in style.css (NO_RULE lists the deliberate exceptions);
  6. the pages carry the whole Content-Security-Policy (POLICY: nothing but the site itself, no base or form target,
     Trusted Types), script-src 'self' on the index and 'none' on the plain and 404 pages, a referrer policy, and no
     file from another host (only a canonical or alternate <link> may name one); the index's JSON-LD block parses
     and says what the software is;
  7. site.js makes no network, storage or eval call and names no outside URL, still has the menu keys, the game pad,
     the viewer (keys, swipe, a modal dialog, #shot-N and history), the controls help, pads outside the standard
     layout, find in page and the tab title, and passes `node --check` when node is on PATH;
  8. accessibility the markup can show: headings never skip a level, every table has a caption and named header
     cells with a scope, a scrollable code block can take the keyboard focus;
  9. contrast, from style.css's colour tokens in both themes: text pairs at least 4.5:1 (WCAG 1.4.3), graphics and
     the focus ring at least 3:1 (1.4.11);
 10. weight and layout shift: every image has a width and a height, a gallery tile shows a WebP copy from a srcset
     (lazy) and links the PNG, the banner is a WebP fetched first (never lazy), the font is the pinned Rajdhani file
     with its licence beside it and is preloaded, and the main page's first view (page, CSS, script, font, banner)
     stays under FIRST_VIEW_BUDGET;
 11. the other published files: 404.html (well-formed, its /project-vekta/ links resolve), sitemap.xml (every <loc>
     on SITE_URL and on a file) and manifest.webmanifest (its icons exist at their sizes).

With --staged the directory is the published root as tools/release/stage_site.py writes it (no link may climb out
with ../).

The checks follow Project Pyrrhus's tools/release/check_site.py (undecuple, MIT), itself after Project Helghast's.

    python3 tools/release/check_site.py [SITE_DIR]             # default: site/
    python3 tools/release/check_site.py --staged _site         # what pages.yml uploads
"""
import hashlib
import html.parser
import json
import struct
import xml.etree.ElementTree as ET
import pathlib
import re
import shutil
import subprocess
import sys

VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
NO_RULE = {"o-no", "obj"}
# 9. the colour pairs of style.css that must contrast, (foreground, background, minimum ratio)
TEXT_PAIRS = [("text", "bg"), ("text", "panel"), ("dim", "bg"), ("dim", "panel"), ("dim", "panel2"), ("dim", "glow"),
              ("link", "bg"), ("link", "panel"), ("steel", "bg"), ("steel", "panel"), ("on-steel", "steel"),
              ("head2", "panel"), ("strong", "panel2"), ("st-ok", "st-ok-b"), ("st-part", "st-part-b"),
              ("st-no", "st-no-b"), ("st-na", "st-na-b")]
GRAPHIC_PAIRS = [("m-ok", "panel"), ("m-part", "panel"), ("m-no", "panel"), ("m-na", "panel"), ("m-ok", "seg"),
                 ("m-part", "seg"), ("m-no", "seg"), ("m-na", "seg"), ("steel-hi", "bg"), ("steel-hi", "panel")]   # hooks that are unstyled on purpose (o-no: the empty objective box)
# 10. the one web font (Rajdhani Bold, latin, from Google Fonts' v17 files; SIL OFL 1.1, site/fonts/OFL.txt)
FONT = "fonts/rajdhani-700.woff2"
FONT_SHA256 = "5b7e4a6f97163c2636724d4de90304fc895653dcfe64c67a7a22f26331ca5c5f"
SITE_URL = "https://gustavomorinaga.github.io/project-vekta/"
SITE_PATH = "/project-vekta/"
# 6. the policy every page carries; {script} is 'self' on the index, 'none' elsewhere
POLICY = {"default-src": "'none'", "script-src": "{script}", "style-src": "'self'", "img-src": "'self'",
          "font-src": "'self'", "manifest-src": "'self'", "base-uri": "'none'", "form-action": "'none'",
          "upgrade-insecure-requests": "", "require-trusted-types-for": "'script'", "trusted-types": "'none'"}
REFERRER = "strict-origin-when-cross-origin"
OUTSIDE_OK = ("canonical", "alternate")     # <link rel> that may name another host (they load nothing)
FIRST_VIEW_BUDGET = 250 * 1024   # index.html + style.css + site.js + the font + banner.webp, in bytes
JS_FORBIDDEN = re.compile(r"\b(fetch|XMLHttpRequest|WebSocket|EventSource|sendBeacon|localStorage|sessionStorage|"
                          r"indexedDB|document\.cookie|eval|Function\s*\(|importScripts|setTimeout\s*\(\s*['\"])")
JS_FEATURES = {
    "menu keys": ("ArrowDown", "ArrowUp", "Enter", "Escape"),
    "game pad": ("getGamepads", "gamepadconnected"),
    "viewer keys": ("ArrowLeft", "ArrowRight"),
    "swipe": ("touchstart", "touchend"),
    "modal dialog": ("showModal", "'cancel'"),
    "shot links and history": ("shot-", "pushState", "popstate"),
    "controls help": ("'help'", ".help-btn"),
    "pad layouts, sticks and the pad used last": ("mapping", "RELEASE", "timestamp"),
    "find in page": ("beforematch", "until-found"),
    "section title focus and tab title": ("document.title", "heading("),
}


class Page(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.ids, self.problems = [], {}, []
        self.links, self.classes, self.scripts, self.styles, self.inline_js = [], set(), [], [], False
        self.sections, self.csp, self.referrer, self.ld, self._ld = [], None, None, [], None
        self.outside = []
        self._sec, self._h2, self._depth = None, None, 0
        self.heads, self.tables, self.captions, self._th = [], 0, 0, None
        self.imgs, self.preloads, self.shots = [], [], []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag not in VOID:
            self.stack.append(tag)
        if "id" in a:
            if a["id"] in self.ids:
                self.problems.append(f"id '{a['id']}' twice")
            self.ids[a["id"]] = tag
        self.classes.update((a.get("class") or "").split())
        if any(k.startswith("on") for k in a):
            self.problems.append(f"<{tag}> has an inline event handler")
        if tag == "meta" and (a.get("http-equiv") or "").lower() == "content-security-policy":
            self.csp = a.get("content", "")
        if tag == "meta" and a.get("name") == "referrer":
            self.referrer = a.get("content")
        if tag == "script":
            if a.get("src"):
                self.scripts.append(a["src"])
            elif a.get("type") == "application/ld+json":
                self._ld = []           # data, not code: the policy does not run it
            else:
                self.inline_js = True
        for k in ("src", "srcset", "data-full") + (("href",) if tag == "link" and a.get("rel") not in OUTSIDE_OK else ()):
            if re.match(r"\s*([a-z]+:|//)", a.get(k) or "", re.I) and not (a.get(k) or "").startswith("data:"):
                self.outside.append(f"<{tag} {k}=\"{a[k]}\">")
        if tag == "link" and "stylesheet" in (a.get("rel") or ""):
            self.styles.append(a.get("href", ""))
        if tag == "link" and a.get("rel") == "preload":
            self.preloads.append(a)
        if tag == "img":
            self.imgs.append(a)
        if tag == "a" and "shot" in (a.get("class") or "").split():
            self.shots.append(a)
        if a.get("data-full"):
            self.links.append((tag, a["data-full"], self._sec))
        for k in ("href", "src"):
            if a.get(k) and not (tag == "link" and a.get("rel") in OUTSIDE_OK):
                self.links.append((tag, a[k], self._sec))
        if a.get("srcset"):
            self.links += [(tag, part.split()[0], self._sec) for part in a["srcset"].split(",")]
        if tag == "section" and "sec" in (a.get("class") or "").split():
            self._sec, self._depth = a.get("id"), len(self.stack) - 1
            self.sections.append([self._sec, ""])
        if tag == "h2" and self._sec:
            self._h2 = []
        if re.fullmatch(r"h[1-6]", tag):
            level = int(tag[1])
            if self.heads and level > self.heads[-1] + 1:
                self.problems.append(f"<{tag}> follows <h{self.heads[-1]}>: a heading level is skipped")
            self.heads.append(level)
        if tag == "table":
            self.tables += 1
        if tag == "caption":
            self.captions += 1
        if tag == "th":
            if a.get("scope") not in ("col", "row"):
                self.problems.append("<th> without scope=\"col\" or \"row\"")
            self._th = []
        if tag == "pre" and a.get("tabindex") != "0":
            self.problems.append("<pre> the keyboard cannot reach (no tabindex=\"0\"): it scrolls on a phone")

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack or self.stack[-1] != tag:
            self.problems.append(f"</{tag}> closes <{self.stack[-1] if self.stack else 'nothing'}>")
            if tag in self.stack:
                while self.stack and self.stack.pop() != tag:
                    pass
            return
        self.stack.pop()
        if tag == "script" and self._ld is not None:
            self.ld.append("".join(self._ld))
            self._ld = None
        if tag == "h2" and self._h2 is not None:
            self.sections[-1][1] = "".join(self._h2).strip()
            self._h2 = None
        if tag == "th" and self._th is not None:
            if not "".join(self._th).strip():
                self.problems.append("<th> with no text (give the column a name, visually hidden if need be)")
            self._th = None
        if tag == "section" and self._sec and len(self.stack) == self._depth:
            self._sec = None

    def handle_data(self, data):
        if self._ld is not None:
            self._ld.append(data)
        if self._h2 is not None:
            self._h2.append(data)
        if self._th is not None:
            self._th.append(data)

    def close(self):
        super().close()
        if self.stack:
            self.problems.append(f"unclosed: {self.stack}")
        if self.captions < self.tables:
            self.problems.append(f"{self.tables - self.captions} table(s) without a <caption>")


def tokens(css):
    """style.css's colour tokens: (dark, light), each {name: #rrggbb}; light is dark with the light block's values."""
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    first = re.search(r":root\s*\{(.*?)\}", css, re.S)
    light = re.search(r"prefers-color-scheme:\s*light\)\s*\{\s*:root\s*\{(.*?)\}", css, re.S)
    pick = lambda body: dict(re.findall(r"--([\w-]+):\s*(#[0-9a-fA-F]{3,6})\b", body or ""))
    dark = pick(first.group(1) if first else "")
    return dark, {**dark, **pick(light.group(1) if light else "")}


def ratio(a, b):
    def lum(h):
        h = h.lstrip("#")
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        ch = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
        ch = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4 for c in ch]
        return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2]
    hi, lo = sorted((lum(a), lum(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def contrast(css):
    problems = []
    for theme, tok in zip(("dark", "light"), tokens(css)):
        for pairs, need in ((TEXT_PAIRS, 4.5), (GRAPHIC_PAIRS, 3.0)):
            for fg, bg in pairs:
                if fg not in tok or bg not in tok:
                    problems.append(f"style.css ({theme}): no --{fg if fg not in tok else bg} token to check")
                    continue
                r = ratio(tok[fg], tok[bg])
                if r < need:
                    problems.append(f"style.css ({theme}): --{fg} on --{bg} is {r:.2f}:1, below {need}:1")
    return problems


def parse(path):
    p = Page()
    p.feed(path.read_text(encoding="utf-8"))
    p.close()
    return p


def section_links(page):
    out = {}
    for _tag, url, sec in page.links:
        if sec:
            out.setdefault(sec, set()).add(url)
    return out


def resolve(site, url):
    """The file a link names, or None for another host. /project-vekta/x is site/x (the 404 page's links)."""
    if url.startswith(SITE_PATH):
        url = url[len(SITE_PATH):] or "index.html"
    elif re.match(r"[a-z]+:|//", url, re.I):
        return None
    path = url.partition("#")[0]
    return (site / path) if path else None


def policy(csp, script):
    """The problems of one page's Content-Security-Policy against POLICY."""
    got = {}
    for part in filter(None, (x.strip() for x in (csp or "").split(";"))):
        name, _, value = part.partition(" ")
        got[name] = " ".join(value.split())
    want = {k: v.format(script=script) for k, v in POLICY.items()}
    out = [f"policy {k} is {got.get(k)!r}, want {v!r}" for k, v in want.items() if got.get(k) != v]
    out += [f"policy has {k}, which POLICY does not" for k in got if k not in want]
    return out


def json_ld(page):
    if len(page.ld) != 1:
        return [f"{len(page.ld)} JSON-LD blocks, want 1"]
    try:
        d = json.loads(page.ld[0])
    except ValueError as e:
        return [f"JSON-LD does not parse: {e}"]
    out = []
    if d.get("@context") != "https://schema.org" or d.get("@type") != "SoftwareApplication":
        out.append("JSON-LD is not a schema.org SoftwareApplication")
    for k in ("name", "description", "url", "operatingSystem", "applicationCategory", "softwareVersion"):
        if not d.get(k):
            out.append(f"JSON-LD has no {k}")
    if d.get("url") != SITE_URL:
        out.append(f"JSON-LD url {d.get('url')!r} is not {SITE_URL}")
    return out


def png_size(path):
    data = path.read_bytes()[:24]
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", data[16:24])


def published(site, staged):
    """11. 404.html, sitemap.xml and manifest.webmanifest."""
    problems = []
    f = site / "404.html"
    if not f.is_file():
        return ["404.html missing"]
    p = parse(f)
    problems += [f"404.html: {x}" for x in p.problems + p.outside]
    problems += [f"404.html: {x}" for x in policy(p.csp, "'none'")]
    if p.scripts or p.inline_js:
        problems.append("404.html: has a script")
    for tag, url, _sec in p.links:
        if not url.startswith(SITE_PATH) and not url.startswith("#") and not re.match(r"[a-z]+:", url):
            problems.append(f"404.html: <{tag}> {url} is relative (the page answers any address: links start {SITE_PATH})")
        t = resolve(site, url)
        if t is not None and not t.exists():
            problems.append(f"404.html: <{tag}> {url} does not resolve")
    try:
        root = ET.parse(site / "sitemap.xml").getroot()
    except (OSError, ET.ParseError) as e:
        problems.append(f"sitemap.xml: {e}")
        root = None
    if root is not None:
        locs = [e.text or "" for e in root.iter() if e.tag.endswith("}loc")]
        if SITE_URL not in locs:
            problems.append(f"sitemap.xml does not list {SITE_URL}")
        for loc in locs:
            if not loc.startswith(SITE_URL):
                problems.append(f"sitemap.xml: {loc} is not on {SITE_URL}")
            elif not (site / (loc[len(SITE_URL):] or "index.html")).is_file():
                problems.append(f"sitemap.xml: {loc} is not a published file")
    try:
        m = json.loads((site / "manifest.webmanifest").read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return problems + [f"manifest.webmanifest: {e}"]
    for k in ("name", "short_name", "start_url", "icons", "theme_color", "background_color"):
        if not m.get(k):
            problems.append(f"manifest.webmanifest has no {k}")
    for icon in m.get("icons", []):
        f = site / icon.get("src", "")
        size = png_size(f) if f.is_file() else None
        if not size or icon.get("sizes") != f"{size[0]}x{size[1]}":
            problems.append(f"manifest.webmanifest: icon {icon.get('src')} is missing or not {icon.get('sizes')}")
    return problems


def check(site, staged=False):
    site = pathlib.Path(site)
    problems = []
    pages = {}
    for name in ("index.html", "plain.html"):
        f = site / name
        if not f.exists():
            problems.append(f"{name} missing")
            continue
        pages[name] = parse(f)
        problems += [f"{name}: {x}" for x in pages[name].problems]
    for name in ("style.css", "plain.css", "site.js"):
        if not (site / name).exists():
            problems.append(f"{name} missing")
    if problems:
        return problems

    # 2. fragments and relative links
    for name, p in pages.items():
        for tag, url, _sec in p.links:
            if staged and url.startswith("../"):
                problems.append(f"{name}: <{tag}> {url} climbs out of the published root")
            if re.match(r"[a-z]+:|//", url, re.I):
                continue
            path, _, frag = url.partition("#")
            if not path:
                if frag and frag not in p.ids:
                    problems.append(f"{name}: #{frag} has no target")
                continue
            target = (site / path).resolve()
            if not target.exists():
                problems.append(f"{name}: <{tag}> {url} does not resolve")
            elif frag and path in pages and frag not in pages[path].ids:
                problems.append(f"{name}: {url}: no #{frag} on {path}")

    # 3 and 6. what each page loads, and its policy
    idx, plain = pages["index.html"], pages["plain.html"]
    for name, p in pages.items():
        if p.inline_js:
            problems.append(f"{name}: inline script")
        for href in p.styles:
            if re.match(r"[a-z]+:|//", href, re.I):
                problems.append(f"{name}: outside stylesheet {href}")
    if idx.scripts != ["site.js"]:
        problems.append(f"index.html: scripts {idx.scripts}, want only site.js")
    if plain.scripts:
        problems.append(f"plain.html: loads scripts {plain.scripts}")
    for name, p in pages.items():
        problems += [f"{name}: {x}" for x in policy(p.csp, "'self'" if p is idx else "'none'")]
        if p.referrer != REFERRER:
            problems.append(f"{name}: referrer policy {p.referrer!r}, want {REFERRER!r}")
        problems += [f"{name}: loads from another host: {x}" for x in p.outside]
    problems += [f"index.html: {x}" for x in json_ld(idx)]
    if plain.ld:
        problems.append("plain.html: has JSON-LD (only the index describes the software)")
    problems += published(site, staged)
    if any(x.get("as") == "font" for x in plain.preloads) or "@font-face" in (site / "plain.css").read_text(encoding="utf-8"):
        problems.append("plain.html: loads web fonts")
    for css in ("style.css", "plain.css"):
        text = (site / css).read_text(encoding="utf-8")
        if "@import" in text:
            problems.append(f"{css}: @import")
        for u in re.findall(r"url\(\s*['\"]?([^)'\"]+)", text):
            if re.match(r"[a-z]+:|//", u, re.I) and not u.startswith("data:"):
                problems.append(f"{css}: outside url({u})")

    # 4. the two pages agree
    if idx.sections != plain.sections:
        problems.append(f"the pages' sections differ: {idx.sections} / {plain.sections}")
    a, b = section_links(idx), section_links(plain)
    for sec in set(a) | set(b):
        if a.get(sec, set()) != b.get(sec, set()):
            problems.append(f"section '{sec}': the links differ between the pages")

    # 5. every class has a rule
    css = (site / "style.css").read_text(encoding="utf-8")
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    selectors = set(re.findall(r"\.([A-Za-z][\w-]*)", css))
    for c in sorted(idx.classes - selectors - NO_RULE - {"js", "ready", "on"}):
        problems.append(f"index.html: class '{c}' has no rule in style.css")

    # 9. contrast of the colour tokens
    problems += contrast((site / "style.css").read_text(encoding="utf-8"))

    # 10. weight and layout shift
    problems += weight(site, pages)

    # 7. site.js
    js = (site / "site.js").read_text(encoding="utf-8")
    code = re.sub(r"/\*.*?\*/", "", js, flags=re.S)
    for m in JS_FORBIDDEN.finditer(code):
        problems.append(f"site.js: forbidden call {m.group(1)}")
    if re.search(r"https?:|//[a-z0-9.-]+\.[a-z]{2,}/", code, re.I):
        problems.append("site.js: names an outside URL")
    if "innerHTML" in code:
        problems.append("site.js: uses innerHTML")
    for what, needles in JS_FEATURES.items():
        missing = [n for n in needles if n not in code]
        if missing:
            problems.append(f"site.js: the {what} is gone (no {', '.join(missing)})")
    for o, c in ("()", "[]", "{}"):
        if code.count(o) != code.count(c):
            problems.append(f"site.js: unbalanced {o}{c}")
    node = shutil.which("node")
    if node:
        r = subprocess.run([node, "--check", str(site / "site.js")], capture_output=True, text=True)
        if r.returncode:
            problems.append("site.js: node --check: " + r.stderr.strip()[:300])
    else:
        print("check_site: SKIP node --check (node not on PATH)")
    return problems


def weight(site, pages):
    problems = []
    for name, p in pages.items():
        for a in p.imgs:
            if not (a.get("width") and a.get("height")):
                problems.append(f"{name}: <img src=\"{a.get('src')}\"> has no width and height (the layout jumps when it loads)")
        for a in p.shots:
            img_ok = a.get("href", "").endswith(".webp") and a.get("data-full", "").endswith(".png")
            if not img_ok:
                problems.append(f"{name}: screenshot link {a.get('href')} is not a WebP copy with its PNG in data-full")
        tiles = [a for a in p.imgs if a.get("sizes") and "banner" not in (a.get("class") or "").split()]
        if len(tiles) != len(p.shots):
            problems.append(f"{name}: {len(p.shots)} screenshot links but {len(tiles)} thumbnails with srcset and sizes")
        for a in tiles:
            if not a.get("src", "").endswith(".webp") or a.get("loading") != "lazy" or " 960w" not in a.get("srcset", ""):
                problems.append(f"{name}: thumbnail {a.get('src')} is not a lazy WebP with a 480w/960w srcset")
        banner = [a for a in p.imgs if "banner" in (a.get("class") or "").split()]
        if len(banner) != 1 or not banner[0].get("src", "").endswith(".webp") or banner[0].get("fetchpriority") != "high" \
                or banner[0].get("loading") == "lazy":
            problems.append(f"{name}: the banner is not one WebP image with fetchpriority=\"high\"")
    f = site / FONT
    if not f.is_file() or hashlib.sha256(f.read_bytes()).hexdigest() != FONT_SHA256:
        problems.append(f"{FONT} is missing or not the pinned Rajdhani file")
    if not (site / "fonts" / "OFL.txt").is_file():
        problems.append("fonts/OFL.txt (the font's licence) is missing")
    css = (site / "style.css").read_text(encoding="utf-8")
    if f'url("{FONT}")' not in css:
        problems.append(f"style.css has no @font-face for {FONT}")
    if not any(x.get("href") == FONT and x.get("as") == "font" and "crossorigin" in x for x in pages["index.html"].preloads):
        problems.append(f"index.html does not preload {FONT} (as=\"font\" crossorigin)")
    banner = [a.get("src") for a in pages["index.html"].imgs if "banner" in (a.get("class") or "").split()]
    first = ["index.html", "style.css", "site.js", FONT] + banner[:1]
    size = sum((site / x).stat().st_size for x in first if (site / x).is_file())
    if size > FIRST_VIEW_BUDGET:
        problems.append(f"the main page's first view is {size // 1024} KB ({', '.join(first)}), over "
                        f"{FIRST_VIEW_BUDGET // 1024} KB")
    return problems


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    staged = "--staged" in argv
    argv = [a for a in argv if a != "--staged"]
    site = pathlib.Path(argv[0]) if argv else pathlib.Path(__file__).resolve().parents[2] / "site"
    problems = check(site, staged)
    for p in problems:
        print("check_site:", p, file=sys.stderr)
    if not problems:
        print(f"check_site: ok ({site})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
