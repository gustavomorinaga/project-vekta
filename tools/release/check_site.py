#!/usr/bin/env python3
"""Checks of the written project site (site/: index.html, plain.html, style.css, plain.css, site.js), on the
repository layout (site/ next to assets/ and docs/screenshots/, as pages.yml stages it).

  1. both pages are well-formed: every tag closed in order, ids unique;
  2. every #fragment resolves on its page, every relative link and image resolves to a file, and an other-page link
     (plain.html#x) finds its fragment there;
  3. nothing loads from outside: no stylesheet but the Google Fonts one (index only), no script but site.js (index
     only), no inline script or event handler, no @import or outside url() in the CSS;
  4. the two pages agree: the same sections in the same order, the same headings, the same links inside them;
  5. every class index.html uses has a selector in style.css (NO_RULE lists the deliberate exceptions);
  6. the pages carry a Content-Security-Policy: script-src 'self' on the index, 'none' on the plain page;
  7. site.js makes no network, storage or eval call and names no outside URL, still has the menu keys, the game pad,
     the viewer (keys, swipe, focus trap, #shot-N and history), and passes `node --check` when node is on PATH.

The checks follow Project Pyrrhus's tools/release/check_site.py (undecuple, MIT), itself after Project Helghast's.

    python3 tools/release/check_site.py [SITE_DIR]      # default: site/
"""
import html.parser
import pathlib
import re
import shutil
import subprocess
import sys

VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
NO_RULE = {"o-no", "obj"}   # hooks that are unstyled on purpose (o-no: the empty objective box)
FONT_CSS = re.compile(r"https://fonts\.googleapis\.com/")
JS_FORBIDDEN = re.compile(r"\b(fetch|XMLHttpRequest|WebSocket|EventSource|sendBeacon|localStorage|sessionStorage|"
                          r"indexedDB|document\.cookie|eval|Function\s*\(|importScripts|setTimeout\s*\(\s*['\"])")
JS_FEATURES = {
    "menu keys": ("ArrowDown", "ArrowUp", "Enter", "Escape"),
    "game pad": ("getGamepads", "gamepadconnected"),
    "viewer keys": ("ArrowLeft", "ArrowRight"),
    "swipe": ("touchstart", "touchend"),
    "focus trap": ("'Tab'",),
    "shot links and history": ("shot-", "pushState", "popstate"),
}


class Page(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.ids, self.problems = [], {}, []
        self.links, self.classes, self.scripts, self.styles, self.inline_js = [], set(), [], [], False
        self.sections, self.csp = [], None
        self._sec, self._h2, self._depth = None, None, 0

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
        if tag == "script":
            if a.get("src"):
                self.scripts.append(a["src"])
            else:
                self.inline_js = True
        if tag == "link" and "stylesheet" in (a.get("rel") or ""):
            self.styles.append(a.get("href", ""))
        for k in ("href", "src"):
            if a.get(k) and not (tag == "link" and a.get("rel") in ("canonical", "preconnect")):
                self.links.append((tag, a[k], self._sec))
        if a.get("srcset"):
            self.links += [(tag, part.split()[0], self._sec) for part in a["srcset"].split(",")]
        if tag == "section" and "sec" in (a.get("class") or "").split():
            self._sec, self._depth = a.get("id"), len(self.stack) - 1
            self.sections.append([self._sec, ""])
        if tag == "h2" and self._sec:
            self._h2 = []

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
        if tag == "h2" and self._h2 is not None:
            self.sections[-1][1] = "".join(self._h2).strip()
            self._h2 = None
        if tag == "section" and self._sec and len(self.stack) == self._depth:
            self._sec = None

    def handle_data(self, data):
        if self._h2 is not None:
            self._h2.append(data)

    def close(self):
        super().close()
        if self.stack:
            self.problems.append(f"unclosed: {self.stack}")


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


def check(site):
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
            if re.match(r"[a-z]+:|//", href, re.I) and not (name == "index.html" and FONT_CSS.match(href)):
                problems.append(f"{name}: outside stylesheet {href}")
    if idx.scripts != ["site.js"]:
        problems.append(f"index.html: scripts {idx.scripts}, want only site.js")
    if plain.scripts:
        problems.append(f"plain.html: loads scripts {plain.scripts}")
    if not idx.csp or "script-src 'self'" not in idx.csp:
        problems.append("index.html: no Content-Security-Policy with script-src 'self'")
    if not plain.csp or "script-src 'none'" not in plain.csp:
        problems.append("plain.html: no Content-Security-Policy with script-src 'none'")
    if any(FONT_CSS.match(h) for h in plain.styles):
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


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    site = pathlib.Path(argv[0]) if argv else pathlib.Path(__file__).resolve().parents[2] / "site"
    problems = check(site)
    for p in problems:
        print("check_site:", p, file=sys.stderr)
    if not problems:
        print(f"check_site: ok ({site})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
