#!/usr/bin/env python3
"""Move the release version everywhere in one go, ready for the release commit and its tag.

    python3 tools/release/bump_version.py [--check] [--date YYYY-MM-DD] [--root DIR] [--notes FILE] X.Y.Z

After Project Pyrrhus's and Project Helghast's tools/release/bump_version.py (undecuple, MIT). VERSION is the single
source (release_version.py); this edits it and every place that writes the version:
  - VERSION;
  - CITATION.cff: version and date-released (added after the title when missing), when the version there differs;
  - README.md: the `<!-- release -->...<!-- /release -->` span becomes the link to the release's page;
  - every one-line `<!-- version -->...<!-- /version -->` span in README.md and docs/ (each X.Y.Z inside becomes the
    new version, so a span may hold a whole release link);
  - CHANGELOG.md: "## [Unreleased]" becomes "## [X.Y.Z] - DATE (pre-release)" ("(release)" from 1.0.0), under a new
    empty "## [Unreleased]" for the next one; left alone when X.Y.Z already has an entry.
Then it writes the project page again (tools/website.py: the download block follows the new entry) and runs the
checks in order: tools/tests/test_version_agree.py, then the release notes for vX.Y.Z (release_notes.py; --notes FILE
keeps them). Exit 1 when one fails (the files are written; fix and run again: a second run with the same version
changes nothing). It refuses a version that is not X.Y.Z or is older than VERSION. --check lists what would change and
writes nothing. Local tool: no network, no git, no tag, no push; the release workflow starts from the tag you push.
"""
import argparse
import datetime
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import release_version  # noqa: E402

REPO = "https://github.com/gustavomorinaga/project-vekta"
MARKER = re.compile(r"(<!-- version -->)(.*?)(<!-- /version -->)")
RELEASE_SPAN = re.compile(r"(<!-- release -->)(.*?)(<!-- /release -->)")
SEMVER = re.compile(r"\d+\.\d+\.\d+")
# [ \t]*$, never \s*$: with re.M, \s* runs over the newline and swallows the blank line after the heading.
UNRELEASED = re.compile(r"^## \[Unreleased\][ \t]*$", re.M)


def kind(version):
    return "pre-release" if version.split(".")[0] == "0" else "release"


def fill_markers(text, version):
    return MARKER.sub(lambda m: m.group(1) + SEMVER.sub(version, m.group(2)) + m.group(3), text)


def release_span(text, version):
    link = f"[v{version}]({REPO}/releases/tag/v{version}), a source {kind(version)}"
    return RELEASE_SPAN.sub(lambda m: m.group(1) + link + m.group(3), text)


def changelog(text, version, date):
    if re.search(rf"^## \[{re.escape(version)}\]", text, re.M):
        return text
    if not UNRELEASED.search(text):
        raise SystemExit("bump_version: CHANGELOG.md has no '## [Unreleased]' entry to release")
    return UNRELEASED.sub(f"## [Unreleased]\n\n## [{version}] - {date} ({kind(version)})", text, count=1)


def citation(text, version, date):
    lines = text.splitlines()
    cur = next((re.sub(r'^version:\s*"?([^"\s]*)"?\s*$', r"\1", x) for x in lines if x.startswith("version:")), None)
    if cur == version:
        return text
    want = {"version": f'version: "{version}"', "date-released": f'date-released: "{date}"'}
    keys = {x.split(":", 1)[0] for x in lines}
    out = []
    for line in lines:
        k = line.split(":", 1)[0]
        out.append(want.get(k, line))
        if k == "title":
            out += [want[x] for x in ("version", "date-released") if x not in keys]
    return "\n".join(out) + "\n"


def edits(root, version, date):
    """{path: new text} for every file whose text changes."""
    root = pathlib.Path(root)
    out = {}

    def edit(p, fn):
        if p.is_file():
            old = p.read_text(encoding="utf-8")
            new = fn(old)
            if new != old:
                out[p] = new

    edit(root / "VERSION", lambda t: version + "\n")
    edit(root / "CITATION.cff", lambda t: citation(t, version, date))
    edit(root / "README.md", lambda t: fill_markers(release_span(t, version), version))
    for p in sorted((root / "docs").rglob("*.md")):
        edit(p, lambda t: fill_markers(t, version))
    edit(root / "CHANGELOG.md", lambda t: changelog(t, version, date))
    return out


def run(root, *cmd):
    return subprocess.run([sys.executable, *map(str, cmd)], capture_output=True, text=True, cwd=root)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("version", help="the new version, X.Y.Z, not older than VERSION")
    ap.add_argument("--check", action="store_true", help="list what would change; write nothing")
    ap.add_argument("--date", default=datetime.date.today().isoformat(), help="the release day (default today)")
    ap.add_argument("--notes", metavar="FILE", help="keep the release notes here (default: checked, not written)")
    ap.add_argument("--root", default=str(ROOT), help="repository root (the tests use a copy)")
    a = ap.parse_args(argv)
    root = pathlib.Path(a.root)
    if not release_version.valid(a.version):
        print(f"bump_version: {a.version!r} is not MAJOR.MINOR.PATCH", file=sys.stderr)
        return 2
    try:
        date = datetime.date.fromisoformat(a.date).isoformat()
    except ValueError:
        date = None
    if date != a.date:
        print(f"bump_version: --date {a.date!r} is not YYYY-MM-DD", file=sys.stderr)
        return 2
    try:
        cur = release_version.read(str(root))
    except ValueError as e:
        print(f"bump_version: {e}", file=sys.stderr)
        return 2
    if release_version.key(a.version) < release_version.key(cur):
        print(f"bump_version: {a.version} is older than the current {cur}", file=sys.stderr)
        return 2
    changed = edits(root, a.version, a.date)
    for p in sorted(changed):
        print(("would change " if a.check else "changed ") + str(p.relative_to(root)))
    if not changed:
        print(f"bump_version: nothing to change (already {a.version})")
    if a.check:
        return 0
    for p, text in changed.items():
        eol = "\r\n" if b"\r\n" in p.read_bytes() else "\n"   # keep each file's own line endings
        p.write_text(text, encoding="utf-8", newline=eol)
    steps = [("website", [root / "tools/website.py", "--root", root]),
             ("test_version_agree", [root / "tools/tests/test_version_agree.py", "--repo", root]),
             ("release_notes", [root / "tools/release/release_notes.py", "--tag", "v" + a.version, "--root", root]
              + (["--out", a.notes] if a.notes else []))]
    for what, cmd in steps:
        r = run(root, *cmd)
        last = (r.stdout.strip().splitlines() or [""])[-1]
        print(f"{what}: " + ("ok" if r.returncode == 0 else "FAILED") + (f" ({last})" if last and what != "release_notes" else ""))
        if r.returncode:
            print((r.stderr or r.stdout)[-2000:], file=sys.stderr)
            return 1
    print(f"bump_version: {a.version} ready; commit, then tag v{a.version} on that commit and push the tag")
    return 0


if __name__ == "__main__":
    sys.exit(main())
