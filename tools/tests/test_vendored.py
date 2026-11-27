#!/usr/bin/env python3
"""Host test for the vendored libraries in third_party/ (minimp3, the FidelityFX SDK's FSR 3.1 part, the Recomp
Achievements SDK): every file git tracks there, but the README.md at the top of each folder and the ps3recomp
submodule, is listed in third_party/SHA256SUMS with the SHA-256 it has, and nothing listed there is missing. Catches a
vendored file edited, added or deleted by accident; a deliberate update rewrites the list in the same commit:

    git ls-files -s third_party | awk '$1!="160000"{print $4}' | grep -v -e '^third_party/[^/]*/README.md$' \
        -e '^third_party/SHA256SUMS$' | LC_ALL=C sort | xargs sha256sum > third_party/SHA256SUMS

Also: each vendored folder has a README.md and a licence file, and the SHA-256 that minimp3's README states agree
with the list."""
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SUMS = "third_party/SHA256SUMS"
TOP_README = re.compile(r"third_party/[^/]+/README\.md")
fails = []

staged = subprocess.run(["git", "ls-files", "-s", "--", "third_party"], cwd=ROOT, capture_output=True, text=True,
                        check=True).stdout.splitlines()
tracked = sorted(line.split("\t", 1)[1] for line in staged
                 if not line.startswith("160000 ") and not TOP_README.fullmatch(line.split("\t", 1)[1])
                 and line.split("\t", 1)[1] != SUMS)

listed = {}
for n, line in enumerate(open(os.path.join(ROOT, SUMS), encoding="utf-8"), 1):
    m = re.fullmatch(r"([0-9a-f]{64})  (\S+)\n?", line)
    if not m:
        fails.append(f"{SUMS}:{n}: not a sha256sum line")
        continue
    listed[m.group(2)] = m.group(1)

for f in tracked:
    if f not in listed:
        fails.append(f"{f}: tracked but not in {SUMS}")
        continue
    with open(os.path.join(ROOT, f), "rb") as fh:
        got = hashlib.sha256(fh.read()).hexdigest()
    if got != listed[f]:
        fails.append(f"{f}: SHA-256 {got[:16]}..., {SUMS} has {listed[f][:16]}...")
for f in sorted(set(listed) - set(tracked)):
    fails.append(f"{f}: in {SUMS} but not tracked")

folders = sorted({f.split("/")[1] for f in tracked})
for d in folders:
    names = os.listdir(os.path.join(ROOT, "third_party", d))
    if "README.md" not in names:
        fails.append(f"third_party/{d}: no README.md (source, version, changes)")
    if not any(x.upper().startswith(("LICENSE", "COPYING")) for x in names):
        fails.append(f"third_party/{d}: no licence file")

readme = open(os.path.join(ROOT, "third_party/minimp3/README.md"), encoding="utf-8").read()
stated = re.findall(r"\|\s*`([^`]+)`\s*\|\s*`([0-9a-f]{64})`\s*\|", readme)
if not stated:
    fails.append("third_party/minimp3/README.md: no SHA-256 table")
for name, h in stated:
    if listed.get(f"third_party/minimp3/{name}") != h:
        fails.append(f"third_party/minimp3/README.md: {name}'s SHA-256 is not the one in {SUMS}")

for f in fails:
    print("FAIL:", f)
print(f"test_vendored: {len(tracked)} files in {len(folders)} folders,", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
