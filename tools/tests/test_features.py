#!/usr/bin/env python3
"""Host test for tools/features.py: docs/FEATURES.md is what docs/features.tsv gives, and malformed rows (a bad
status, a wrong column count, a '|' in a cell, a pc or port row with a PS3 column) are refused."""
import importlib.util
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("features", os.path.join(ROOT, "tools", "features.py"))
features = importlib.util.module_from_spec(spec)
spec.loader.exec_module(features)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "features.py"), "--check"], capture_output=True, text=True)
check(r.returncode == 0, "the committed outputs are current: " + r.stdout.strip())

HEAD = "# checked\t2026-10-30\n" + "\t".join(features.COLUMNS) + "\n"
GOOD = ("ps3\tCampaign\tLevel 1\tfirst level\tworks\t\tplayed\tLevel 1\n"
        "pc\t\tFOV\t\tthe game's\t\t`fov`\tField of view\n"
        "port\t\tInstaller\t\tnot yet\t: from source\troadmap\tInstaller\n")


def problems(text):
    with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False, encoding="utf-8") as f:
        f.write(text)
    features.TSV = f.name
    try:
        return features.load()
    finally:
        os.unlink(f.name)


checked, rows, p = problems(HEAD + GOOD)
check(not p and len(rows) == 3 and checked == "2026-10-30", f"a good table loads: {p}")
md = features.markdown(checked, rows)
check("| Campaign | Level 1 | first level | **works** | played |" in md, "a ps3 row becomes a section 1 row")
check("| FOV | the game's | `fov` |" in md, "a pc row becomes a section 2 row")
check("| Installer | **not yet**: from source | roadmap |" in md, "a port row becomes a section 3 row")
for bad, what in [
    ("ps3\tCampaign\tLevel 1\tfirst level\tfine\t\tplayed\t\n", "an unknown status"),
    ("ps3\tCampaign\tLevel 1\tfirst level\tworks\n", "a short row"),
    ("ps3\tCampaign\tLevel 1 | 2\tfirst level\tworks\t\tplayed\t\n", "a '|' in a cell"),
    ("pc\tGraphics\tFOV\t\tthe game's\t\t`fov`\t\n", "a pc row with an area"),
    ("port\t\tInstaller\t\tsoon\t\troadmap\t\n", "a port row with an unknown status"),
    ("port\tDist\tInstaller\t\tnot yet\t\troadmap\t\n", "a port row with an area"),
    ("mod\t\tFOV\t\tx\t\ty\t\n", "an unknown kind"),
]:
    check(problems(HEAD + bad)[2], f"{what} is refused")
check(problems("\t".join(features.COLUMNS) + "\n" + GOOD)[2], "a table without its checked date is refused")

print("test_features:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
