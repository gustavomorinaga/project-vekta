#!/usr/bin/env python3
"""Host test for recomplauncher.json (installer/recomplauncher.json, tools/release/recomplauncher.py and its schema):
the source and the document it makes pass the schema and its own references; prepare.version is VERSION and the exit
codes are port/kz1hd/kz_exit.h's, 0 being "ok"; the prepare and DLSS arguments are ones tools/release/launch.py takes
and pass on; the DLSS licence is firstrun.py's; the optional EBOOT's SHA-256 is docs/GETTING-FILES.md's; nothing in it
asks for firmware; and the checker refuses what the schema does not allow. The programs it names are in each release
folder's manifest: tools/tests/test_payload.py. No game data."""
import copy
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import firstrun  # noqa: E402
import launch  # noqa: E402
import recomplauncher as rl  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


r = subprocess.run([sys.executable, str(ROOT / "tools" / "release" / "recomplauncher.py"), "--check"],
                   capture_output=True, text=True)
check(r.returncode == 0 and "recomplauncher.json: ok" in r.stdout, f"--check passes: {r.stdout}{r.stderr}")
doc = rl.build()
version = (ROOT / "VERSION").read_text().strip()
check(doc["prepare"]["version"] == version and rl.build(version="9.9.9")["prepare"]["version"] == "9.9.9",
      "prepare.version is the release's VERSION")
check(doc["exit_codes"] == {"0": "ok", **{str(c): t for c, t in launch.EXIT.values()}},
      "exit_codes are kz_exit.h's table (launch.py's), and 0 is a normal quit")
args = doc["prepare"]["args"] + doc["prepare"]["options"][0]["args"]
filled = [a.replace("{input:game}", "/games/NPUA80856").replace("{input:eboot}", "/x/EBOOT.elf") for a in args]
a = launch.parser().parse_args(["--prepare", *filled])
check(a.prepare and a.game == "/games/NPUA80856" and a.eboot == "/x/EBOOT.elf" and a.dlss == "accept",
      f"launch.py --prepare takes the file's arguments as filled in: {filled}")
check(launch.firstrun_args("/nowhere", a, {})[:4] == ["--game", "/games/NPUA80856", "--eboot", "/x/EBOOT.elf"] and
      "--dlss" in launch.firstrun_args("/nowhere", a, {}), "and passes them on to firstrun.py")
check(all(a.endswith(("={input:game}", "={input:eboot}")) or "{" not in a for a in doc["prepare"]["args"]),
      "each input is one argument (--x={input:id}), so an optional one left out leaves nothing behind")
opt = doc["prepare"]["options"][0]
check(opt["license"]["url"] == firstrun.DLSS_LICENSE_URL and opt["gpu"] == "nvidia",
      "the DLSS tick shows NVIDIA's licence, firstrun.py's link, to NVIDIA cards only")
row = re.search(r"\| `EBOOT\.elf` \| ([\d,]+) \| `([0-9a-f]{64})` \|", (ROOT / "docs" / "GETTING-FILES.md").read_text())
eboot = next(i for i in doc["inputs"] if i["id"] == "eboot")
check(row and row.group(2) in eboot["label"] and row.group(1) in eboot["label"] and eboot.get("optional"),
      "the optional EBOOT.elf names docs/GETTING-FILES.md's size and SHA-256")
game = next(i for i in doc["inputs"] if i["id"] == "game")
check(game["role"] == "game" and game["formats"] == [{"type": "folder"}] and "NPUA80856" in game["label"],
      "the game input is the installed NPUA80856 folder")
check(not re.search(r"firmware|dev_flash|\.pup|libsre", rl.text(doc), re.I), "nothing in it asks for firmware")
check(doc["start"]["program"] == [launch_name for launch_name in ("Project Vekta.exe", "project-vekta")] and
      "args" not in doc["start"], "start: the folders' one program each, with no arguments (it plays)")

schema = json.loads((ROOT / "tools" / "release" / "recomplauncher.schema.json").read_text())
bad = [("prepare.version", lambda d: d["prepare"].__setitem__("version", "0.6")),
       ("an unknown top-level key", lambda d: d.__setitem__("version", "1")),
       ("a role", lambda d: d["inputs"][0].__setitem__("role", "firmware")),
       ("an exit code", lambda d: d["exit_codes"].__setitem__("x", "y")),
       ("a save outside the folder", lambda d: d["saves"].append("{home}/x")),
       ("no start program", lambda d: d["start"].__setitem__("program", []))]
for what, spoil in bad:
    d = copy.deepcopy(doc)
    spoil(d)
    check(rl.check(d, schema), f"the checker refuses {what}")

print("test_recomplauncher:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
