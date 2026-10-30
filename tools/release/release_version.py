#!/usr/bin/env python3
"""The release version: one source of truth for every place that shows it. Standard library only.

    release_version.py [--repo DIR]                 print the version (VERSION)
    release_version.py --check [--tag vX.Y.Z]       VERSION is MAJOR.MINOR.PATCH with no suffix; with --tag the release
                                                    tag is "v" + that version (a tag is never moved)

VERSION, at the repository root, holds MAJOR.MINOR.PATCH and nothing else: the version the next or current release
carries. tools/release/bump_version.py moves it and every place that writes it (CITATION.cff, the README's release
span, the CHANGELOG entry, the project page); tools/tests/test_version_agree.py checks that they agree. Tags are
vMAJOR.MINOR.PATCH; every release is a GitHub pre-release until v1.0.0; no suffixes.
(After Project Pyrrhus's and Project Helghast's tools/release/release_version.py, by undecuple, MIT.)
Exit 0 = ok, 1 = a check failed, 2 = usage.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
VERSION_FILE = "VERSION"
STRICT = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)")


def valid(v):
    """'0.1.0' -> True; '0.1', '0.1.0-rc1', 'v0.1.0', '01.1.0' -> False."""
    return bool(STRICT.fullmatch(v or ""))


def key(v):
    return tuple(int(x) for x in v.split("."))


def read(repo=REPO):
    """VERSION, or raises ValueError with the reason."""
    try:
        with open(os.path.join(repo, VERSION_FILE), encoding="utf-8") as f:
            v = f.read().strip()
    except OSError as e:
        raise ValueError(f"{VERSION_FILE}: {e.strerror}") from e
    if not valid(v):
        raise ValueError(f"{VERSION_FILE} holds {v!r}: expected MAJOR.MINOR.PATCH with no suffix")
    return v


def from_tag(tag):
    """'v0.1.0' -> '0.1.0'; anything else -> None."""
    return tag[1:] if tag.startswith("v") and valid(tag[1:]) else None


def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=REPO)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--tag", default="")
    a = ap.parse_args(argv)
    try:
        v = read(a.repo)
    except ValueError as e:
        print(f"FAIL {e}", file=out)
        return 1
    if a.tag and from_tag(a.tag) != v:
        print(f"FAIL tag {a.tag!r} is not v{v} (VERSION); tags are vMAJOR.MINOR.PATCH, never moved", file=out)
        return 1
    print(f"ok {v}" if a.check else v, file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
