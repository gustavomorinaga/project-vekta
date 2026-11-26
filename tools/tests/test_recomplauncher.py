#!/usr/bin/env python3
"""Host test for recomplauncher.json (installer/recomplauncher.json, tools/release/recomplauncher.py and its schema):
the source and the document it makes pass the schema and its own references; prepare.version is VERSION and the exit
codes are port/kz1hd/kz_exit.h's, 0 being "ok"; the prepare and DLSS arguments are ones tools/release/launch.py takes
and pass on; the DLSS licence is firstrun.py's; the optional EBOOT's SHA-256 is docs/GETTING-FILES.md's; nothing in it
asks for firmware; and the checker refuses what the schema does not allow. The programs it names are in each release
folder's manifest: tools/tests/test_payload.py. No game data."""
import copy
import json
import os
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
# The disc instead of the game folder, in the same box: a folder with PS3_GAME, or DATA000.PKG, goes the --disc way
# (launch.is_disc), which wins over the RPCS3 route
import tempfile  # noqa: E402
with tempfile.TemporaryDirectory() as tmp:
    disc_dir = pathlib.Path(tmp, "Trilogy")
    (disc_dir / "PS3_GAME").mkdir(parents=True)
    pkg = pathlib.Path(tmp, "DATA000.PKG")
    pkg.write_bytes(b"")
    installed = pathlib.Path(tmp, "NPUA80856")
    (installed / "USRDIR").mkdir(parents=True)
    for given in (disc_dir, pkg):
        filled = [a.replace("{input:game}", str(given)).replace("{input:eboot}", "") for a in doc["prepare"]["args"]]
        fa = launch.firstrun_args("/nowhere", launch.parser().parse_args(["--prepare", *filled]), {})
        check(fa[:2] == ["--disc", os.path.abspath(given)] and "--rpcs3" not in fa and "--game" not in fa,
              f"the disc in the game box ({given.name}) gives firstrun.py --disc: {fa}")
    fa = launch.firstrun_args("/nowhere", launch.parser().parse_args(["--prepare", f"--game={installed}"]), {})
    check(fa[:2] == ["--game", str(installed)] and "--disc" not in fa, f"an installed folder stays --game: {fa}")
# Easy Setup (RecompLauncher's Launcher Support box, lib.rs profile_covers, as the site counts it): a required input of
# role game or disc. 0.7.0 made both the game folder and the disc optional, and lost the tick.
check({i["id"]: i.get("optional", False) for i in doc["inputs"]} == {"game": False, "eboot": True},
      "one required game input (the folder, the disc or its package): RecompLauncher's Easy Setup")
opt = doc["prepare"]["options"][0]
check(opt["license"]["url"] == firstrun.DLSS_LICENSE_URL and opt["gpu"] == "nvidia",
      "the DLSS tick shows NVIDIA's licence, firstrun.py's link, to NVIDIA cards only")
row = re.search(r"\| `EBOOT\.elf` \| ([\d,]+) \| `([0-9a-f]{64})` \|", (ROOT / "docs" / "GETTING-FILES.md").read_text())
eboot = next(i for i in doc["inputs"] if i["id"] == "eboot")
check(row and row.group(2) in eboot["label"] and row.group(1) in eboot["label"] and eboot.get("optional"),
      "the optional EBOOT.elf names docs/GETTING-FILES.md's size and SHA-256")
game = next(i for i in doc["inputs"] if i["id"] == "game")
check(game["role"] == "game" and game["formats"] == [{"type": "folder"}, {"type": "file", "extensions": ["pkg"]}]
      and "NPUA80856" in game["label"], "the game input is the NPUA80856 folder, the disc's folder or its package")
check(not re.search(r"firmware|dev_flash|\.pup|libsre", rl.text(doc), re.I), "nothing in it asks for firmware")
# prepare.done: the file a finished build always leaves, so the launcher knows the game is built (RecompLauncher's
# "No prepare.done" warning, 2026-11-20): firstrun.py's last step writes work/build-stamp.json after a good compile
import inspect  # noqa: E402
check(doc["prepare"]["done"] == ["{install}/work/build-stamp.json"] and firstrun.STEPS[-1].name == "stamp" and
      'c.w("build-stamp.json")' in inspect.getsource(firstrun.STEPS[-1].after),
      "prepare.done is work/build-stamp.json, which firstrun.py's last step (stamp) writes after a good build")
check(doc["start"]["program"] == [launch_name for launch_name in ("Project Vekta.exe", "project-vekta")] and
      "args" not in doc["start"], "start: the folders' one program each, with no arguments (it plays)")

# dumps: the launcher reads a file input whole only when it has dumps, to show "matches the verified copy"; the
# EBOOT's is the one docs/GETTING-FILES.md names (size here; its SHA-256 is checked above), the package's the US
# PlayStation Store one (measured from the maintainer's own copy, 2026-11-24), on the game input, whose file is the
# package (a folder is never read whole).
dumps = {i["id"]: i.get("dumps") for i in doc["inputs"]}
check(dumps["eboot"][0]["size"] == int(row.group(1).replace(",", "")) and dumps["game"][0]["size"] > 10**9,
      f"dumps: the EBOOT's size is GETTING-FILES.md's, the game input's is the package's: {dumps}")

# config and settings: the PC page's file (kz1hd_pc.ini, key=value lines with no sections, in the run folder) and
# rows that write what the game reads: each key is one the game keeps (kVideo or the mouse rows), each default is the
# game's own, each choice a value the game's own tables have, so the launcher and the PC page agree.
pc = (ROOT / "port" / "kz1hd" / "pcoptions.cpp").read_text()
aspects = (ROOT / "port" / "kz1hd" / "view_aspect.h").read_text()
kvideo = dict(re.findall(r'\{ "(\w+)", "([^"]*)" \}', pc.split("kVideo[] = {", 1)[1].split("};", 1)[0]))
game_defaults = {**kvideo, "mouse_yaw": "0.5000", "mouse_pitch": "0.5000", "mouse_invert": "0"}


def table(name, text=pc):
    m = re.search(r"\b" + name + r"\[[A-Z_]*\]\s*=\s*\{([^}]*)\}", text)
    return re.findall(r'"([^"]*)"', m.group(1)) if m else []


tables = {"display": table("kDisplays"), "vsync": table("kVsyncs"), "anisotropic": table("kAniso"),
          "temporal": table("kTemporal"), "speakers": table("kSpeakers"), "language": table("kLanguages"),
          "reflex": table("kReflex"), "aspect": table("kViewAspect", aspects),
          "scale": re.findall(r"\d+", re.search(r"kScales\[\] = \{([^}]*)\}", pc).group(1)),
          "fps_limit": re.findall(r"\d+", re.search(r"kFps\[\] = \{([^}]*)\}", pc).group(1)),
          "widescreen": ["on", "off"], "intro": ["on", "off"], "upscaler": ["linear", "fsr"],
          "antialiasing": ["mlaa", "off"], "controllers": ["merged", "separate"], "overlay": ["off", "mangohud"],
          "mouse_invert": ["0", "1"]}
check(all(len(v) >= 2 for v in tables.values()), f"the game's tables were found in pcoptions.cpp: {tables}")
check(doc["config"] == [{"id": "main", "format": "ini", "file": [{"path": "{install}/work/vfs/kz1hd_pc.ini"}]}] and
      "{install}/work/vfs/kz1hd_pc.ini" in doc["saves"] and 'kIni = "kz1hd_pc.ini"' in pc,
      "config: kz1hd_pc.ini in the run folder, as INI (the launcher edits it line by line)")
written = {k for s in doc["settings"] for k in rl.setting_keys(s)}
check(written <= set(game_defaults), f"every key is one the game keeps: {sorted(written - set(game_defaults))}")
check(set(kvideo) - written <= {"refresh"},
      f"every PC page setting is offered but refresh (the fullscreen mode's Hz): {sorted(set(kvideo) - written)}")
for s in doc["settings"]:
    for k, v in s["defaults"].items():
        same = v == game_defaults[k] or (k.startswith("mouse_") and float(v) == float(game_defaults[k]))
        check(same, f"{s['id']}: the default {k}={v} is the game's ({game_defaults[k]})")
    for c in s.get("choices", []):
        for k, v in c["set"].items():
            if k in tables:
                check(v in tables[k], f"{s['id']}: {c['label']} writes {k}={v}, a value the game knows {tables[k]}")
    if "slider" in s:
        sl = s["slider"]
        check(sl["min"] <= float(s["defaults"][sl["key"]]) <= sl["max"] and sl.get("step", 1) > 0,
              f"{s['id']}: the slider holds its default")
dep = {s["id"]: s.get("when") for s in doc["settings"]}
check(dep["aspect"] == {"widescreen": "on"} and dep["hud_position"] == {"widescreen": "on"} and
      dep["sharpness"] == {"upscaler": "fsr"} and dep["antialiasing"] == {"temporal": "off"},
      "the dependent rows are greyed as on the PC page (pc_deps.h)")
check(not rl.settings_problems({"config": doc["config"], "settings": [
          {"id": "a", "label": "A", "choices": [{"label": "x", "set": {"k": "1"}}, {"label": "y", "set": {"k": "2"}}],
           "defaults": {"k": "1"}}]}) and
      rl.settings_problems({"config": doc["config"], "settings": [
          {"id": "a", "label": "A", "when": {"nope": "1"}, "slider": {"key": "k", "min": 0, "max": 1},
           "defaults": {"j": "1"}}]}),
      "the settings checker takes a good setting and refuses a when on no key and defaults of another key")

schema = json.loads((ROOT / "tools" / "release" / "recomplauncher.schema.json").read_text())
bad = [("prepare.version", lambda d: d["prepare"].__setitem__("version", "0.6")),
       ("an unknown top-level key", lambda d: d.__setitem__("version", "1")),
       ("a role", lambda d: d["inputs"][0].__setitem__("role", "firmware")),
       ("an exit code", lambda d: d["exit_codes"].__setitem__("x", "y")),
       ("a save outside the folder", lambda d: d["saves"].append("{home}/x")),
       ("no start program", lambda d: d["start"].__setitem__("program", [])),
       ("no prepare.done", lambda d: d["prepare"].pop("done")),
       ("a setting with no defaults", lambda d: d["settings"][0].pop("defaults")),
       ("a slider with no key", lambda d: next(s for s in d["settings"] if "slider" in s)["slider"].pop("key")),
       ("a config format", lambda d: d["config"][0].__setitem__("format", "yaml"))]
for what, spoil in bad:
    d = copy.deepcopy(doc)
    spoil(d)
    check(rl.check(d, schema), f"the checker refuses {what}")

print("test_recomplauncher:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
