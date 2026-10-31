#!/usr/bin/env python3
"""Host test for tools/release/: the version rules (release_version.py); the notes from a CHANGELOG entry and their
refusals (release_notes.py); bump_version.py on a copy of the repository (what it changes, that a second run changes
nothing, that an older version is refused); the source archive (pack_source.py) on a small git repository with a
submodule: reproducible, tracked files only, the submodule's files and its pinned commit in, a dirty tree refused;
and the published-release check (release_check.py) on a good release and broken ones. No network, no game."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
REL = ROOT / "tools" / "release"
sys.path.insert(0, str(REL))
import pack_source  # noqa: E402
import release_check  # noqa: E402
import release_notes  # noqa: E402
import release_version  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def run(*cmd, cwd=None):
    return subprocess.run([sys.executable, *map(str, cmd)], capture_output=True, text=True, cwd=cwd)


def git(cwd, *args):
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@example.com", GIT_AUTHOR_DATE="2026-10-30T12:00:00Z",
               GIT_COMMITTER_DATE="2026-10-30T12:00:00Z")
    return subprocess.run(["git", "-c", "commit.gpgsign=false", "-c", "protocol.file.allow=always", *args], cwd=cwd,
                          env=env, check=True, capture_output=True, text=True).stdout


# release_version
check(release_version.valid("0.1.0") and not any(map(release_version.valid, ["0.1", "v0.1.0", "0.1.0-rc1", "01.1.0"])),
      "only MAJOR.MINOR.PATCH is a version")
check(release_version.from_tag("v0.2.3") == "0.2.3" and release_version.from_tag("0.2.3") is None, "tags are vX.Y.Z")
check(release_version.valid(release_version.read(str(ROOT))), "VERSION holds a version")

# release_notes
LOG = """# Changelog

## [Unreleased]

### Added
- Something new, see [the docs](docs/BUILDING.md)

## [0.2.0] - 2026-11-05 (pre-release)

### Added
- A feature.

### Fixed
- A crash
  on two lines.

## [0.1.0] - 2026-11-01 (pre-release)

### Added
- The first one.

## [First public tree] - 2026-10-30

### Added
- The port.
"""
es = release_notes.entries(LOG)
check(es["0.2.0"]["items"][-1] == ("Fixed", "A crash on two lines."), "a wrapped item is joined")
body = release_notes.notes("0.2.0", es["0.2.0"], release_notes.first_release("0.2.0", es))
check("Project Vekta 0.2.0, a pre-release" in body and "New in 0.2.0:\n- A feature." in body
      and "Fixed:\n- A crash on two lines." in body, "notes list the new items and the fixes apart")
check("/blob/v0.2.0/docs/BUILDING.md" in body and "project-vekta-0.2.0-source.tar.gz" in body,
      "notes link the tag's files and name the archive")
check(release_notes.first_release("0.1.0", es) and not release_notes.first_release("0.2.0", es),
      "the first release is the one with no published version before it")
with tempfile.TemporaryDirectory() as tmp:
    (pathlib.Path(tmp) / "CHANGELOG.md").write_text(LOG)
    r = run(REL / "release_notes.py", "--tag", "v0.1.0", "--root", tmp)
    check(r.returncode == 0 and "- The port." in r.stdout, "the first release carries the first public tree")
    check("only on the maintainer's machine" in r.stdout, "v0.1.0's notes say no other machine has built it")
    check("only on the maintainer's machine" not in body, "later notes do not")
    check(run(REL / "release_notes.py", "--tag", "v0.3.0", "--root", tmp).returncode == 1, "a tag with no entry is refused")
    r = run(REL / "release_notes.py", "--preview", "0.3.0", "--root", tmp)
    check(r.returncode == 0 and "Something new" in r.stdout, "a preview takes [Unreleased]")
    (pathlib.Path(tmp) / "CHANGELOG.md").write_text(LOG.replace("## [Unreleased]", "## [0.3.0]"))
    check(run(REL / "release_notes.py", "--tag", "v0.3.0", "--root", tmp).returncode == 1,
          "an entry with no date and note is not a release")

# bump_version on a copy of the repository (the files it edits and the checks it runs)
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    files = subprocess.run(["git", "ls-files", "-co", "--exclude-standard"], cwd=ROOT, capture_output=True,
                           text=True, check=True).stdout.split("\n")
    for rel in files:
        if rel and not rel.startswith("third_party/") and (ROOT / rel).is_file():
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, tmp / rel)
    log = tmp / "CHANGELOG.md"
    log.write_text(log.read_text().replace("## [Unreleased]\n", "## [Unreleased]\n\n### Fixed\n- A test item.\n", 1))
    cur = release_version.read(str(tmp))
    new = ".".join(map(str, release_version.key(cur)[:2])) + f".{release_version.key(cur)[2] + 1}"
    r = run(tmp / "tools/release/bump_version.py", new, "--date", "2026-12-24", "--root", tmp)
    check(r.returncode == 0, "bump_version runs: " + r.stdout[-300:] + r.stderr[-600:])
    check((tmp / "VERSION").read_text() == new + "\n", "VERSION moves")
    check(f"## [Unreleased]\n\n## [{new}] - 2026-12-24 (pre-release)\n\n### Fixed\n- A test item." in log.read_text(),
          "[Unreleased] becomes the entry, under a new empty [Unreleased]")
    check(f'version: "{new}"' in (tmp / "CITATION.cff").read_text(), "CITATION.cff takes the version")
    check(f"releases/tag/v{new}" in (tmp / "README.md").read_text(), "the README links the release")
    check(f"releases/tag/v{new}" in (tmp / "site/index.html").read_text(), "the page offers the release")
    r = run(tmp / "tools/release/bump_version.py", new, "--date", "2026-12-24", "--root", tmp)
    check(r.returncode == 0 and "nothing to change" in r.stdout, "a second run changes nothing")
    check(run(tmp / "tools/release/bump_version.py", cur, "--root", tmp).returncode == 2, "an older version is refused")

# pack_source on a small repository with a submodule
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    sub, top = tmp / "sub", tmp / "top"
    sub.mkdir()
    git(sub, "init", "-q")
    (sub / "CMakeLists.txt").write_text("project(x)\n")
    git(sub, "add", ".")
    git(sub, "commit", "-q", "-m", "s")
    pin = git(sub, "rev-parse", "HEAD").strip()
    top.mkdir()
    git(top, "init", "-q")
    (top / "VERSION").write_text("1.2.3\n")
    (top / "run.sh").write_text("#!/bin/sh\n")
    (top / "run.sh").chmod(0o755)
    (top / ".gitignore").write_text("work/\n")
    git(top, "submodule", "add", "-q", str(sub), "third_party/ps3recomp")
    git(top, "add", ".")
    git(top, "commit", "-q", "-m", "t")
    (top / "work").mkdir()
    (top / "work" / "big.bin").write_text("ignored")
    (top / "untracked.txt").write_text("not tracked")
    a, _ = pack_source.pack(top, "1.2.3", tmp / "a")
    b, _ = pack_source.pack(top, "1.2.3", tmp / "b")
    check(a.read_bytes() == b.read_bytes(), "the archive is reproducible")
    with tarfile.open(a) as t:
        names = t.getnames()
        mode = t.getmember("project-vekta-1.2.3/run.sh").mode
        pinned = t.extractfile("project-vekta-1.2.3/third_party/ps3recomp.pin").read().decode().strip()
    check("project-vekta-1.2.3/third_party/ps3recomp/CMakeLists.txt" in names, "the submodule's files are in")
    check(pinned == pin, "the submodule's pinned commit is in")
    check(not any("work/" in n or "untracked" in n for n in names), "only tracked files are in")
    check(mode == 0o755, "executables keep their mode")
    sha = (tmp / "a" / (a.name + ".sha256")).read_text().split()[0]
    check(sha == hashlib.sha256(a.read_bytes()).hexdigest(), "the .sha256 matches")
    (top / "run.sh").write_text("#!/bin/sh\necho changed\n")
    try:
        pack_source.pack(top, "1.2.3", tmp / "c")
        check(False, "a dirty tree is refused")
    except SystemExit:
        pass

# release_check
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    v = "0.4.0"
    (tmp / "VERSION").write_text(v + "\n")
    (tmp / "site").mkdir()
    (tmp / "README.md").write_text(f"[v{v}](https://x/releases/tag/v{v})\n")
    (tmp / "site/index.html").write_text(f'<a href="https://x/releases/tag/v{v}">v{v}</a>\n')
    assets = tmp / "assets"
    assets.mkdir()
    src = pack_source.name(v)
    (assets / src).write_bytes(b"archive")
    h = hashlib.sha256(b"archive").hexdigest()
    (assets / (src + ".sha256")).write_text(f"{h}  {src}\n")
    (assets / "SHA256SUMS").write_text(f"{h}  {src}\n")
    rel = {"tag_name": f"v{v}", "name": f"v{v}", "draft": False, "prerelease": True, "body": f"Project Vekta {v}",
           "assets": [{"name": n} for n in release_check.expected_assets(v)]}
    (tmp / "rel.json").write_text(json.dumps(rel))
    r = run(REL / "release_check.py", "--tag", f"v{v}", "--release", tmp / "rel.json", "--assets", assets, "--root", tmp)
    check(r.returncode == 0, "a good release passes: " + r.stdout)
    for change, what in [({"draft": True}, "a draft"), ({"prerelease": False}, "a 0.x release that is not a pre-release"),
                         ({"name": "Release"}, "a title that is not the tag"),
                         ({"assets": [{"name": src}]}, "missing assets")]:
        check(release_check.check_release(dict(rel, **change), f"v{v}", v), f"{what} is reported")
    check(release_check.check_release(rel, "v0.4.1", v), "a tag that is not VERSION is reported")
    (assets / "SHA256SUMS").write_text(f"{'0' * 64}  {src}\n")
    check(release_check.check_hashes(assets, v), "a wrong SHA256SUMS is reported")
    (tmp / "README.md").write_text("no link\n")
    check(release_check.check_links(tmp, f"v{v}"), "a README without the release link is reported")

print("test_release:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
