#!/usr/bin/env python3
"""Host test for tools/release/check_site.py: the committed site passes; on a copy, each kind of fault is caught (a
broken fragment, a missing image, an outside script or stylesheet, an inline handler, a page without its policy, the
two pages drifting apart, a class without a rule, a network call or a lost feature in site.js, a skipped heading level,
a table without a caption or a named, scoped header, a code block the keyboard cannot reach, a colour token pair below
its contrast minimum, an image without its size, a PNG banner or one not fetched first, a thumbnail that is not a
lazy WebP, a changed or unloaded font, Google Fonts coming back, a policy missing a directive or with one too many, no
referrer policy, an image or icon from another host, a JSON-LD block that does not parse or says the wrong thing, a 404
page with a relative or broken link or a script, a sitemap entry off the site or on no file, a manifest icon that is
missing or of another size); and tools/release/stage_site.py's published layout passes check_site --staged, with the
old /site/ address sending the visitor on and no link climbing out of the root."""
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


def faulty(rel, old, new, what, binary_target=None):
    """Copy site/, assets/ and docs/screenshots/, make one edit, and expect check_site to report it (binary_target: a
    file whose last byte is changed as well)."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        shutil.copytree(ROOT / "site", tmp / "site")
        shutil.copytree(ROOT / "assets", tmp / "assets")
        shutil.copytree(ROOT / "docs/screenshots", tmp / "docs/screenshots")
        f = tmp / "site" / rel
        text = f.read_text(encoding="utf-8")
        check(old in text, f"the fixture has '{old}' in {rel}")
        f.write_text(text.replace(old, new), encoding="utf-8")
        if binary_target:
            b = tmp / "site" / binary_target
            data = b.read_bytes()
            b.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        check(check_site.check(tmp / "site"), f"check_site catches {what}")


faulty("index.html", 'href="#install"', 'href="#nowhere"', "a fragment with no target")
faulty("index.html", 'src="img/banner.webp"', 'src="img/missing.webp"', "a missing image")
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
faulty("index.html", "<h3>Docs</h3>", "<h5>Docs</h5>", "a skipped heading level")
faulty("index.html", '<span class="vh">Item</span>', "", "a header cell with no text")
faulty("plain.html", '<caption class="vh">The PC features</caption>', "", "a table without a caption")
faulty("index.html", 'scope="col"', "", "a header cell without a scope")
faulty("index.html", '<pre tabindex="0"', "<pre", "a scrollable code block the keyboard cannot reach")
faulty("style.css", "--m-part: #9c720a", "--m-part: #c9930f", "a light-theme meter colour below 3:1")
faulty("style.css", "--st-no: #ff8a6a", "--st-no: #6a3a30", "a dark-theme status text below 4.5:1")
faulty("site.js", "showModal", "show", "a viewer that is no longer a modal dialog")
faulty("site.js", "'hidden', 'until-found'", "'hidden', ''", "hidden sections the browser's find no longer reaches")
faulty("index.html", 'class="help-btn"', 'class="help-button"', "the Controls button gone")
faulty("index.html", 'fetchpriority="high" ', "", "a banner the browser does not fetch first")
faulty("index.html", 'width="1280" height="320"', "", "an image without its size")
faulty("index.html", 'loading="lazy" decoding="async"', 'decoding="async"', "a thumbnail that is not lazy")
faulty("index.html", '<link rel="preload" href="fonts/rajdhani-700.woff2" as="font" type="font/woff2" crossorigin>', "",
       "the font not preloaded")
faulty("index.html", '<link rel="stylesheet" href="style.css">',
       '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Rajdhani">'
       '<link rel="stylesheet" href="style.css">', "Google Fonts coming back")
faulty("fonts/OFL.txt", "SIL Open Font License", "", "a changed font file", binary_target="fonts/rajdhani-700.woff2")

faulty("index.html", "base-uri 'none'; ", "", "a policy without base-uri")
faulty("plain.html", "trusted-types 'none'", "trusted-types 'none'; connect-src *", "a policy with a directive too many")
faulty("plain.html", "; require-trusted-types-for 'script'", "", "the plain page without Trusted Types")
faulty("index.html", '<meta name="referrer" content="strict-origin-when-cross-origin">', "", "no referrer policy")
faulty("index.html", 'src="img/banner.webp"', 'src="https://x.org/banner.webp"', "an image from another host")
faulty("index.html", '<link rel="icon" href="img/icon-192.png"', '<link rel="icon" href="https://x.org/i.png"',
       "an icon from another host")
faulty("index.html", '"@type":"SoftwareApplication"', '"@type":"SoftwareApplication",', "JSON-LD that does not parse")
faulty("index.html", '"@type":"SoftwareApplication"', '"@type":"WebPage"', "JSON-LD that is not the software")
faulty("404.html", 'href="/project-vekta/plain.html"', 'href="plain.html"', "a relative link on the 404 page")
faulty("404.html", 'href="/project-vekta/plain.css"', 'href="/project-vekta/gone.css"', "a broken link on the 404 page")
faulty("404.html", "</body>", '<script src="/project-vekta/site.js"></script></body>', "a script on the 404 page")
faulty("sitemap.xml", "<loc>https://gustavomorinaga.github.io/project-vekta/</loc>",
       "<loc>https://example.org/</loc>", "a sitemap entry off the site")
faulty("sitemap.xml", "-1920.webp</image:loc>", "-9999.webp</image:loc>", "a sitemap image on no file")
faulty("manifest.webmanifest", '"sizes": "192x192"', '"sizes": "512x512"', "a manifest icon of another size")

# the published layout (stage_site.py): the root is the page, /site/ sends the visitor on, nothing climbs out
spec = importlib.util.spec_from_file_location("stage_site", ROOT / "tools/release/stage_site.py")
stage_site = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage_site)
with tempfile.TemporaryDirectory() as tmp:
    out = pathlib.Path(tmp) / "_site"
    check(not stage_site.stage(ROOT, out), "stage_site stages the site")
    check(not check_site.check(out, staged=True), f"the staged site passes --staged: {check_site.check(out, staged=True)}")
    idx = (out / "index.html").read_text(encoding="utf-8")
    check("../" not in idx and 'data-full="docs/screenshots/' in idx, "the staged index links docs/ from the root")
    check((out / "assets/social-preview.png").is_file(), "the social preview is published")
    check('url=../"' in (out / "site/index.html").read_text(encoding="utf-8"), "/site/ sends the visitor to the root")
    check(stage_site.stage(ROOT, out), "stage_site refuses a directory that is not empty")
    plain = (out / "plain.html").read_text(encoding="utf-8")
    check('="docs/screenshots/' in plain, "the staged plain page links docs/ from the root")
    (out / "plain.html").write_text(plain.replace('="docs/screenshots/', '="../_site/docs/screenshots/', 1), encoding="utf-8")
    check(any("climbs out" in x for x in check_site.check(out, staged=True)),
          "check_site --staged catches a link that climbs out of the root")

print("test_site:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
