#!/usr/bin/env python3
"""Stage the published site: site/ becomes the root of https://gustavomorinaga.github.io/project-vekta/.

In the checkout the pages sit in site/ and load ../assets/ and ../docs/screenshots/; published, they sit at the root:
  OUT/                  every file of site/ (index.html, plain.html, 404.html, sitemap.xml, the manifest, CSS, JS,
                        fonts/, img/), the pages' ../assets/ and ../docs/ links made root-relative
  OUT/assets/, docs/    only the files the pages name (the press badges, the social preview, the full-size PNGs)
  OUT/site/             the old address: index.html and plain.html send the visitor to the new one
pages.yml uploads OUT after tools/release/check_site.py --staged OUT.

    python3 tools/release/stage_site.py _site            # OUT must not exist or be empty
    python3 tools/release/stage_site.py --root DIR _site # another repository root (the host test's copies)
"""
import argparse
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOP = ("assets", "docs")
# a file the pages name under assets/ or docs/, in a relative link or in an absolute one on the site's address
NAMED = re.compile(r"(?<![\w-])(?:\.\./)?((?:assets|docs)/[\w@./-]+\.(?:png|svg|webp|jpg))")
REDIRECT = ("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; base-uri 'none'; form-action 'none'\">\n"
            "<meta name=\"robots\" content=\"noindex\">\n<meta http-equiv=\"refresh\" content=\"0; url={to}\">\n"
            "<link rel=\"canonical\" href=\"https://gustavomorinaga.github.io/project-vekta/{page}\">\n"
            "<title>Project Vekta has moved</title>\n</head>\n<body>\n"
            "<p>The page moved: <a href=\"{to}\">Project Vekta</a>.</p>\n</body>\n</html>\n")


def stage(root, out):
    """Write OUT; returns the problems (none when it is done)."""
    site = root / "site"
    if not (site / "index.html").is_file():
        return [f"{site}/index.html missing: run tools/website.py"]
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        return [f"{out} exists and is not an empty directory"]
    shutil.copytree(site, out, dirs_exist_ok=True)
    problems, named = [], set()
    for page in sorted(out.glob("*.html")):
        text = page.read_text(encoding="utf-8")
        named.update(NAMED.findall(text))
        for top in TOP:
            text = text.replace(f"../{top}/", f"{top}/")
        if re.search(r"(?:href|src|srcset|data-full)=\"[^\"]*\.\./", text):
            problems.append(f"{page.name}: a link still climbs out with ../")
        page.write_text(text, encoding="utf-8")
    for rel in sorted(named):
        src = root / rel
        if not src.is_file():
            problems.append(f"the pages name {rel}, which is not in the repository")
            continue
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out / rel)
    (out / "site").mkdir(exist_ok=True)
    for name, to, page in (("index.html", "../", ""), ("plain.html", "../plain.html", "plain.html")):
        (out / "site" / name).write_text(REDIRECT.format(to=to, page=page), encoding="utf-8")
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", type=pathlib.Path)
    ap.add_argument("--root", type=pathlib.Path, default=ROOT)
    a = ap.parse_args(argv)
    problems = stage(a.root.resolve(), a.out)
    for p in problems:
        print(f"stage_site: {p}", file=sys.stderr)
    if not problems:
        n = sum(1 for f in a.out.rglob("*") if f.is_file())
        print(f"stage_site: wrote {a.out} ({n} files)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
