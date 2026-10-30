#!/usr/bin/env python3
"""The GitHub release notes for a tag, written from its CHANGELOG entry, so nobody writes them by hand.

    python3 tools/release/release_notes.py --tag vX.Y.Z [--out FILE] [--root DIR]
    python3 tools/release/release_notes.py --preview X.Y.Z [--out FILE]   # the release workflow's dry run

After Project Pyrrhus's and Project Helghast's tools/release/release_notes.py (undecuple, MIT), with this project's
wording and assets:
  Project Vekta X.Y.Z, a pre-release: partly playable, see docs/RELEASE-CRITERIA.md.
  New in X.Y.Z: <the entry's Added/Changed/... items, one bullet each>
  Fixed: <its fixes>                       (part of the list above for the first published release, which also
                                           carries the entries before the first version: the first public tree)
  See CHANGELOG.md for the full list.
  <the fixed paragraph: a source release, what you need, none of it included, the SHA-256 sums>
Links point at the tag's own files. A tag with no CHANGELOG entry, an entry with no items, or an entry that is not a
release (not dated with "(pre-release)" or "(release)", such as [Unreleased]) is refused: exit 1. --preview takes the
version's entry or, when it has none or an empty one, the [Unreleased] one. Exit 0 = notes written, 1 = refused,
2 = usage. Standard library only.
"""
import argparse
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import release_version  # noqa: E402

ROOT = HERE.parents[1]
REPO = "https://github.com/gustavomorinaga/project-vekta"
STATUS = "partly playable"   # the page's headline while Levels 2 to 11 are partial (tools/website.py)
HEADING = re.compile(r"## \[([^\]]+)\](?:[ \t]+-[ \t]+(\d{4}-\d\d-\d\d))?[ \t]*(?:\(([^)]*)\))?[ \t]*$")
NEW_SECTIONS = ("Added", "Changed", "Removed", "Deprecated", "Security")
RELEASED = ("pre-release", "release")


def entries(text):
    """{name: {date, note, items: [(section, text)]}} for every '## [...]' heading of the whole file."""
    out, cur, section = {}, None, ""
    for line in text.splitlines():
        m = HEADING.match(line)
        if m:
            cur = {"date": m.group(2), "note": (m.group(3) or "").strip(), "items": []}
            out.setdefault(m.group(1), cur)
            section = ""
        elif cur is None:
            continue
        elif line.startswith("### "):
            section = line[4:].strip()
        elif line.startswith("- "):
            cur["items"].append((section, line[2:].strip()))
        elif line.startswith("  ") and cur["items"] and line.strip():   # a wrapped item line
            s, t = cur["items"][-1]
            cur["items"][-1] = (s, t + " " + line.strip())
    return out


def released(e):
    return bool(e["date"]) and e["note"] in RELEASED


def sentence(t, blob):
    """An item as a sentence: a full stop added when missing; relative links point at the tag's files."""
    t = re.sub(r"\]\((?!https?:|#|mailto:)\.?/?([^)\s]+)\)", lambda m: f"]({blob}/{m.group(1)})", t.strip())
    return t if t.rstrip(")\"'`").endswith((".", "!", "?")) else t + "."


def first_release(version, es):
    """True when no earlier version was published: the first release has nothing to fix or install over."""
    return not any(release_version.valid(v) and release_version.key(v) < release_version.key(version) and released(e)
                   for v, e in es.items())


def before_versions(es):
    """Items of the entries that are not versions (the first public tree): the first release brings them to players."""
    return [i for name, e in es.items() if name != "Unreleased" and not release_version.valid(name) for i in e["items"]]


def notes(version, entry, first=False):
    blob = f"{REPO}/blob/v{version}"
    new = [sentence(t, blob) for s, t in entry["items"] if s in NEW_SECTIONS or not s]
    fixed = [sentence(t, blob) for s, t in entry["items"] if s == "Fixed"]
    kind = "a pre-release" if version.split(".")[0] == "0" else "a release"
    bullets = lambda items: "\n".join(f"- {t}" for t in items)  # noqa: E731
    if first:
        lists = [f"New in {version}:\n{bullets(new + fixed)}"]
    else:
        lists = ([f"New in {version}:\n{bullets(new)}"] if new else []) + ([f"Fixed:\n{bullets(fixed)}"] if fixed else [])
    return "\n\n".join([
        f"Project Vekta {version}, {kind}: {STATUS}, see [docs/RELEASE-CRITERIA.md]({blob}/docs/RELEASE-CRITERIA.md).",
        *lists,
        f"See [CHANGELOG.md]({blob}/CHANGELOG.md) for the full list.",
        f"A source release: project-vekta-{version}-source.tar.gz holds this tag's tree with ps3recomp at its pinned "
        "commit, and builds the port on your own Linux machine (and the Windows exe from there): "
        f"[docs/BUILDING.md]({blob}/docs/BUILDING.md). You need your own Killzone HD (NPUA80856) from the Killzone "
        "Trilogy disc with its decrypted EBOOT and data archive, and the decrypted libsre and dev_flash of your own PS3 "
        "firmware; none of them is included or downloaded. What each must contain: "
        f"[docs/GETTING-FILES.md]({blob}/docs/GETTING-FILES.md). SHA-256 sums: SHA256SUMS (and the .sha256 file).",
    ]) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--tag", help="the release tag, vX.Y.Z")
    g.add_argument("--preview", metavar="X.Y.Z", help="dry run: the version's entry, else [Unreleased]")
    ap.add_argument("--out", help="write the notes here (default: standard output)")
    ap.add_argument("--root", default=str(ROOT), help="repository root")
    a = ap.parse_args(argv)
    version = release_version.from_tag(a.tag) if a.tag else (a.preview if release_version.valid(a.preview) else None)
    if version is None:
        print(f"release_notes: {a.tag or a.preview!r} is not {'vX.Y.Z' if a.tag else 'X.Y.Z'}", file=sys.stderr)
        return 2
    p = pathlib.Path(a.root) / "CHANGELOG.md"
    if not p.is_file():
        print(f"release_notes: no CHANGELOG.md at {p.parent}", file=sys.stderr)
        return 1
    es = entries(p.read_text(encoding="utf-8"))
    e = es.get(version)
    if a.preview and (e is None or not e["items"]):
        e = es.get("Unreleased") or e
    if e is None:
        print(f"release_notes: CHANGELOG.md has no entry for {version}; tools/release/bump_version.py turns "
              "[Unreleased] into it", file=sys.stderr)
        return 1
    if a.tag and not released(e):
        print(f"release_notes: the CHANGELOG entry for {version} is not a release ({e['note'] or 'no date'})",
              file=sys.stderr)
        return 1
    if not e["items"]:
        print(f"release_notes: the CHANGELOG entry for {version} lists nothing", file=sys.stderr)
        return 1
    first = first_release(version, es)
    body = notes(version, dict(e, items=e["items"] + (before_versions(es) if first else [])), first)
    if a.out:
        out = pathlib.Path(a.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(body, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
