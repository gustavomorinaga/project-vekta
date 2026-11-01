#!/usr/bin/env python3
"""Read-only check that a published GitHub release and the repository at its tag agree.

After Project Pyrrhus's and Project Helghast's tools/release/release_check.py (undecuple, MIT), with this project's
names, version source and assets. Run by .github/workflows/release-check.yml on `release: published` (and by hand
through workflow_dispatch); standard library only, so it is host-tested (tools/tests/test_release_check.py). It reads;
it never edits a release, pushes or tags.

  release_check.py --tag vX.Y.Z --release FILE|-  [--assets DIR] [--root DIR]

--release is the release JSON (GitHub REST: tag_name, name, draft, prerelease, body, assets[].name). Checks:
  1 tag = "v" + VERSION at the checked-out tag (release_version.py, the one version source)
  2 published (not a draft), pre-release exactly while the version is 0.x, title = tag, body names the version
  3 assets are exactly the ones release.yml publishes: project-vekta-<version>-source.tar.gz (pack_source.py), the
    Windows setup program project-vekta-<version>-setup.exe (build_setup.py) and the Windows folder as a zip,
    project-vekta-<version>-windows.zip (assemble_payload.py --zip), a .sha256 for each, and SHA256SUMS
  4 (with --assets) each .sha256 matches its file and agrees with SHA256SUMS, and SHA256SUMS lists exactly those
    files
  5 (with --root) README.md and site/index.html link releases/tag/<tag>
Prints one line per problem; exit 0 only when there are none.
"""
import argparse
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pack_source  # noqa: E402
import release_version  # noqa: E402

SUMS = "SHA256SUMS"


def files(version):
    """The release's files, each published with a .sha256 beside it and listed in SHA256SUMS."""
    return [pack_source.name(version), f"project-vekta-{version}-setup.exe", f"project-vekta-{version}-windows.zip"]


def expected_assets(version):
    return [n for f in files(version) for n in (f, f + ".sha256")] + [SUMS]


def check_release(rel, tag, version):
    errs = []
    if tag != "v" + version:
        errs.append(f"tag {tag} is not v{version} (VERSION)")
    if rel.get("tag_name") != tag:
        errs.append(f"release tag_name {rel.get('tag_name')!r} is not {tag!r}")
    if rel.get("draft"):
        errs.append("release is still a draft")
    want_pre = version.split(".")[0] == "0"
    if bool(rel.get("prerelease")) != want_pre:
        errs.append(f"prerelease is {bool(rel.get('prerelease'))} but version {version} needs {want_pre}")
    if rel.get("name") != tag:
        errs.append(f"release title {rel.get('name')!r} is not the tag {tag!r}")
    if version not in (rel.get("body") or ""):
        errs.append(f"release body does not name {version}")
    have = [a.get("name") for a in rel.get("assets", [])]
    want = expected_assets(version)
    errs += [f"missing asset {n}" for n in want if n not in have]
    errs += [f"unexpected asset {n}" for n in have if n not in want]
    errs += [f"duplicate asset {n}" for n in sorted({n for n in have if have.count(n) > 1})]
    return errs


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_sums(path):
    """{name: hash} from 'HASH  NAME' / 'HASH *NAME' lines; unreadable lines become '?bad line' keys."""
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.fullmatch(r"([0-9a-fA-F]{64}) [ *](.+?)\r?\n?", line)
            if m:
                out[m.group(2)] = m.group(1).lower()
            elif line.strip():
                out["?bad line: " + line.strip()] = ""
    return out


def check_hashes(d, version):
    sums_path = os.path.join(d, SUMS)
    if not os.path.exists(sums_path):
        return [f"{SUMS} not downloaded"]
    sums = parse_sums(sums_path)
    errs = [f"{SUMS}: {k[1:]}" for k in sums if k.startswith("?")]
    want = files(version)
    errs += [f"{SUMS} lists {k}, which the release does not carry" for k in sums
             if not k.startswith("?") and k not in want]
    for n in want:
        p = os.path.join(d, n)
        if not os.path.exists(p):
            errs.append(f"{n} not downloaded")
            continue
        actual = sha256_file(p)
        if sums.get(n) != actual:
            errs.append(f"{SUMS} entry for {n} is {sums.get(n)!r}, file is {actual}")
        sp = p + ".sha256"
        if not os.path.exists(sp):
            errs.append(f"{n}.sha256 not downloaded")
            continue
        own = parse_sums(sp)
        if own.get(n) != actual:
            errs.append(f"{n}.sha256 says {own.get(n)!r}, file is {actual}")
    return errs


def check_links(root, tag):
    errs = []
    for rel in ("README.md", os.path.join("site", "index.html")):
        p = os.path.join(root, rel)
        if not os.path.exists(p):
            errs.append(f"{rel} missing at the tag")
            continue
        with open(p, encoding="utf-8") as f:
            if f"releases/tag/{tag}" not in f.read():
                errs.append(f"{rel} does not link releases/tag/{tag}")
    return errs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tag", required=True)
    ap.add_argument("--release", required=True, help="release JSON file, or - for stdin")
    ap.add_argument("--assets", help="folder with the downloaded assets")
    ap.add_argument("--root", default=release_version.REPO, help="repository root at the tag")
    a = ap.parse_args(argv)
    if a.release == "-":
        rel = json.load(sys.stdin)
    else:
        with open(a.release, encoding="utf-8") as f:
            rel = json.load(f)
    try:
        version = release_version.read(a.root)
    except ValueError as e:
        print(f"release-check: FAIL: {e}")
        return 1
    errs = check_release(rel, a.tag, version)
    if a.assets:
        errs += check_hashes(a.assets, version)
    errs += check_links(a.root, a.tag)
    for e in errs:
        print("release-check: FAIL: " + e)
    if not errs:
        print(f"release-check: {a.tag} OK")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
