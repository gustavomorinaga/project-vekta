#!/usr/bin/env python3
"""Host test: every mention of this project's own release version agrees with VERSION.

    python3 tools/tests/test_version_agree.py [--repo DIR]

After Project Pyrrhus's and Project Helghast's tools/release/test_version_agree.py (undecuple, MIT). The pattern is
narrow on purpose: only places that state the release version are read, so dependency versions (ps3recomp v0.18.0,
GE-Proton 11, action versions) and the CITATION date never match. A mention counts when it is
  - the CITATION.cff key   version: "X"
  - a link to a release    releases/tag/vX
  - prose   "version X", "release X", "pre-release X", "Vekta X"  (X with an optional v)
  and the 40 characters before a prose match name no dependency or other project (ps3recomp, Proton, Wine, FFmpeg,
  Project Helghast, ...). The top-level
files, docs/, site/ and .github/ are read; CHANGELOG.md (every version's history), third_party, licenses and binary
files are not.
"""
import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
X = r"(\d+\.\d+\.\d+)(?!\d|\.\d)"
PATTERNS = [
    ("citation", re.compile(r'^version:\s*"?' + X, re.M)),
    ("release link", re.compile(r"releases/tag/v" + X)),
    ("prose", re.compile(r"\b(?:[Vv]ersion|release|Vekta)\s+[`*\"']{0,2}v?" + X)),
]
DEPENDENCY = re.compile(r"ps3recomp|Proton|Wine|FFmpeg|LLVM|clang|Python|SDL|CMake|Vulkan|driver|firmware|toolchain|"
                        r"MinGW|glslang|gitleaks|ruff|shellcheck|helghast|pyrrhus|undecuple|tokisaki|upstream", re.I)
TEXT = (".md", ".txt", ".html", ".cff", ".yml", ".yaml", ".json")
SKIP_DIRS = {"third_party", "licenses", ".git", "assets", "__pycache__", "screenshots", "work"}


def mentions(text):
    """[(line number, what, version)] for each release-version mention in text."""
    out = []
    for no, line in enumerate(text.splitlines(), 1):
        for what, pat in PATTERNS:
            for m in pat.finditer(line):
                if what == "prose" and DEPENDENCY.search(line[max(0, m.start() - 40):m.start()]):
                    continue
                out.append((no, what, m.group(1)))
    return out


def files(repo):
    out = [os.path.join(repo, n) for n in sorted(os.listdir(repo)) if os.path.isfile(os.path.join(repo, n))]
    for sub in ("docs", "site", ".github"):
        for d, dirs, names in os.walk(os.path.join(repo, sub)):
            dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS)
            out += [os.path.join(d, n) for n in sorted(names)]
    return [p for p in out if p.lower().endswith(TEXT) and not os.path.basename(p).upper().startswith("CHANGELOG")]


def stale(repo, version):
    """['relative/path:line: <what> X (VERSION is Y)'] for every disagreeing mention."""
    found = []
    for p in files(repo):
        try:
            with open(p, encoding="utf-8") as f:
                text = f.read()
        except (OSError, UnicodeDecodeError):
            continue
        found += [f"{os.path.relpath(p, repo)}:{no}: {what} {v} (VERSION is {version})"
                  for no, what, v in mentions(text) if v != version]
    return found


FAILS = []


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"\n     {str(detail)[:600]}"))
    if not cond:
        FAILS.append(what)


def selftest():
    bad = 'version: "0.0.9"\nFrom version 0.0.9 on.\nSee releases/tag/v0.0.9.\nProject Vekta v0.0.9 is out.\n'
    check([m[2] for m in mentions(bad)] == ["0.0.9"] * 4, "stale mentions are all found", mentions(bad))
    fine = ('ps3recomp version 0.18.0, GE-Proton release 11.1.0.\ndate-released: "2026-10-30"\nversion: "0.1.0"\n'
            "release v0.1.0 is out\n")
    got = [m[2] for m in mentions(fine)]
    check(got == ["0.1.0", "0.1.0"], "dependency versions and the date are left alone", got)
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "docs"))
        open(os.path.join(tmp, "VERSION"), "w").write("0.2.0\n")
        open(os.path.join(tmp, "README.md"), "w").write("ps3recomp v0.18.0\nSince version 0.2.0.\n")
        open(os.path.join(tmp, "CHANGELOG.md"), "w").write("## [0.1.0]\nversion 0.1.0\n")
        open(os.path.join(tmp, "docs", "A.md"), "w").write("From version 0.1.0 it ships.\n")
        s = stale(tmp, "0.2.0")
        check(len(s) == 1 and s[0].startswith(os.path.join("docs", "A.md")), "the one stale mention is found", s)


def main(argv):
    repo = argv[argv.index("--repo") + 1] if "--repo" in argv else ROOT
    selftest()
    with open(os.path.join(repo, "VERSION"), encoding="utf-8") as f:
        version = f.read().strip()
    s = stale(repo, version)
    check(len(files(repo)) > 3, f"{len(files(repo))} files read")
    check(not s, f"every release-version mention agrees with VERSION ({version})", "\n     ".join(s))
    print("test_version_agree: " + ("FAIL" if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
