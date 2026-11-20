#!/usr/bin/env python3
"""Host test for the first run's checks of the place it builds in and of the kind of each file (parity items A3-A5):
tools/check-game-files.py refuses a path with [ ] or ;, a folder inside OneDrive and a PC clock behind the port's
files, names an encrypted file or the .edat given as the archive by what they are, and firstrun.py's check step
passes on those lines as reasons; a port folder that was moved gets a fresh build and its saved paths pointed at the
new place; the CPU line checks this CPU against the build's x86-64 level (KZ_MARCH). No game data."""
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import firstrun  # noqa: E402

spec = importlib.util.spec_from_file_location("cgf", ROOT / "tools" / "check-game-files.py")
cgf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cgf)

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    good = tmp / "Games" / "Project Vekta"
    (good / "tools").mkdir(parents=True)
    (good / "VERSION").write_text("0.5.0\n")
    now = time.time()
    os.utime(good / "VERSION", (now, now))
    check(cgf.place_problems(str(good), str(good / "work"), environ={}, now=now) == [], "a plain folder: no problem")

    p = cgf.place_problems(str(tmp / "Vekta [v0.5]"), str(tmp / "Vekta [v0.5]" / "work"), environ={}, now=now)
    check(len(p) == 2 and all(x.startswith("FAIL") and "[ ]" in x and "CMake" in x for x in p),
          f"[ and ] in the path are refused, for the port and the work folder: {p}")
    p = cgf.place_problems(str(tmp / "a;b"), "", environ={}, now=now)
    check(len(p) == 1 and " ; " in p[0] + " ", f"; is refused: {p}")

    od = tmp / "OneDrive - Contoso"
    p = cgf.place_problems(str(od / "Vekta"), "", environ={"OneDriveCommercial": str(od)}, now=now)
    check(len(p) == 1 and "OneDrive" in p[0] and str(od) in p[0], f"a folder inside OneDrive is refused: {p}")
    p = cgf.place_problems(str(tmp / "OneDriveX" / "Vekta"), "", environ={"OneDrive": str(tmp / "OneDrive")}, now=now)
    check(p == [], f"a sibling whose name only starts like OneDrive's is fine: {p}")

    p = cgf.place_problems(str(good), "", environ={}, now=now - 3 * 86400)
    check(len(p) == 1 and "clock" in p[0], f"a clock three days behind the port's files is refused: {p}")
    p = cgf.place_problems(str(good), "", environ={}, now=now - 3600)
    check(p == [], f"an hour behind (another time zone's file times) is fine: {p}")

    fl = tmp / "dev_flash"
    (fl / "vsh" / "etc").mkdir(parents=True)
    check(cgf.firmware_version(str(fl)) is None, "no version.txt: no firmware version")
    (fl / "vsh" / "etc" / "version.txt").write_text("release:04.9000:\nbuild:1:x\n")
    check(cgf.firmware_version(str(fl)) == "4.90", "version.txt's release line gives 4.90")
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "check-game-files.py"), "--quick"], capture_output=True,
                       text=True, env=dict(os.environ, PS3_DEV_FLASH=str(fl), KZ_GAME=str(tmp / "none")))
    check(r.returncode == 1 and "WRONG    firmware:" in r.stdout and "4.90" in r.stdout and "4.93" in r.stdout,
          f"a dev_flash of another system software stops the check and names both versions: {r.stdout[:300]}")
    reasons = firstrun.Runner(firstrun.Ctx(root=str(ROOT), paths={}, work=str(tmp / "w")), firstrun.STEPS[:1],
                              log=type("L", (), {"write": lambda self, x: None})(), out=open(os.devnull, "w"),
                              quiet=True)
    reasons.why_failed(firstrun.STEPS[0], r.stdout.splitlines(), 1)
    check("dev_flash" in reasons.fail_inputs, "firstrun asks for dev_flash again after a wrong firmware")
    (fl / "vsh" / "etc" / "version.txt").write_text("release:04.9300:\n")
    check(cgf.firmware_version(str(fl)) == "4.93", "and 4.93")

    check("still encrypted" in cgf.wrong_kind("EBOOT.elf", b"SCE\0") and "--rpcs3" in cgf.wrong_kind("EBOOT.elf",
          b"SCE\0"), "an encrypted EBOOT is named so, with the RPCS3 route")
    check("Leave data_ps3.psarc unset" in cgf.wrong_kind("data_ps3.psarc", b"NPD\0"),
          "the .edat given as the decrypted archive: leave it unset")

    # The script: the FAIL lines come first and stop it; firstrun's check step gives them as reasons
    env = dict(os.environ, KZ_GAME=str(tmp / "none"), KZ_EBOOT=str(tmp / "none"), KZ_PSARC=str(tmp / "none"))
    eboot = tmp / "EBOOT.elf"
    eboot.write_bytes(b"SCE\0" + b"\0" * 60)
    env["KZ_EBOOT"] = str(eboot)
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "check-game-files.py"), "--quick", "--root",
                        str(tmp / "x[1]"), "--work", str(tmp / "w")], capture_output=True, text=True, env=env)
    lines = r.stdout.splitlines()
    check(r.returncode == 1 and lines[0].startswith("FAIL") and any("WRONG    EBOOT.elf" in x and "still encrypted" in x
          for x in lines), f"the script: the place first, then the files: {lines[:4]}")
    c = firstrun.Ctx(root=str(ROOT), paths={}, work=str(tmp / "work"))
    runner = firstrun.Runner(c, firstrun.STEPS[:1], log=type("L", (), {"write": lambda self, x: None})(),
                             out=open(os.devnull, "w"), quiet=True)
    reasons = runner.why_failed(firstrun.STEPS[0], lines, 1)
    check(any(x.startswith("FAIL") for x in reasons) and "eboot" in runner.fail_inputs,
          f"firstrun's check step reports the FAIL line and asks for the EBOOT again: {reasons}")
    check(firstrun.STEPS[0].commands(c)[0][-4:] == ["--root", str(ROOT), "--work", str(tmp / "work")],
          "firstrun's check step passes the port and work folders")

    # A moved port folder: the CMake cache names the old one
    old, new = tmp / "old", tmp / "new"
    (new / "port" / "kz1hd").mkdir(parents=True)
    (new / "Game").mkdir()
    (new / "Game" / "EBOOT.elf").write_text("x")
    work = new / "work"
    b = work / ("build-win" if os.name == "nt" else "build")
    b.mkdir(parents=True)
    (b / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={old / 'port' / 'kz1hd'}\n")
    (b / "old.o").write_text("")
    elsewhere = tmp / "elsewhere" / "dev_flash"
    firstrun.write_paths(str(work), {"eboot": str(old / "Game" / "EBOOT.elf"), "dev_flash": str(elsewhere)})
    c = firstrun.Ctx(root=str(new), paths=firstrun.resolve_paths(str(new), str(work), {}), work=str(work))
    check(firstrun.moved_from(c) == str(old), f"the old folder comes from the CMake cache: {firstrun.moved_from(c)}")
    check(firstrun.heal_moved_paths(c) == str(old), "heal_moved_paths reports the move")
    saved = firstrun.read_paths(str(work))
    check(saved["eboot"] == str(new / "Game" / "EBOOT.elf") and c.paths["eboot"] == saved["eboot"],
          f"a saved path inside the old folder points into the new one: {saved}")
    check(saved["dev_flash"] == str(elsewhere), "a path outside the old folder stays as it was")
    msg = firstrun.before_compile(c)
    check(msg and "moved" in msg and not b.exists(), f"the build folder made in the old place is emptied: {msg}")
    check(firstrun.moved_from(c) == "", "no cache, no move")

# The CPU line (the build's x86-64 level, port/kz1hd/cpu_check.c): from /proc/cpuinfo's flags on Linux, from the
# launcher's --cpu-check exit code on Windows; a FAIL labelled "CPU:", which firstrun.py ends the first run on
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    hsw = "fpu cx8 cmov mmx fxsr sse sse2 pni ssse3 cx16 sse4_1 sse4_2 popcnt lahf_lm avx xsave avx2 bmi1 bmi2 fma f16c abm movbe"
    for name, flags, want in (("Nehalem", "fpu sse sse2 pni ssse3 cx16 sse4_1 sse4_2 popcnt lahf_lm", 2),
                              ("Sandy Bridge", "pni ssse3 cx16 sse4_1 sse4_2 popcnt lahf_lm avx xsave", 2),
                              ("Haswell", hsw, 3),
                              ("Skylake-SP", hsw + " avx512f avx512bw avx512cd avx512dq avx512vl", 4),
                              ("Core 2", "pni ssse3 cx16 sse4_1 lahf_lm", 1)):
        info = tmp / "cpuinfo"
        info.write_text(f"processor\t: 0\nflags\t\t: {flags}\n\nprocessor\t: 1\n")
        got = cgf.cpu_level(None, windows=False, cpuinfo=str(info))
        check(got == want, f"/proc/cpuinfo of a {name}: x86-64-v{got}, want v{want}")
    check(cgf.cpu_level(None, windows=False, cpuinfo=str(tmp / "none")) is None, "no /proc/cpuinfo: not told")
    check(cgf.cpu_level(str(tmp), windows=True) is None, "Windows without the launcher (a Git clone): not told")
    (tmp / "Project Vekta.exe").write_bytes(b"MZ")
    seen = []

    def fake_run(argv, timeout):
        seen.append(argv)
        return subprocess.CompletedProcess(argv, 2)
    check(cgf.cpu_level(str(tmp), windows=True, run=fake_run) == 2 and seen[0][1:] == ["--cpu-check"]
          and seen[0][0].endswith("Project Vekta.exe"), f"Windows: the launcher's --cpu-check exit code: {seen}")
    fail = cgf.cpu_line(2, 3)
    check(fail.startswith("FAIL     CPU: ") and "x86-64-v3" in fail and "x86-64-v2" in fail,
          f"a v2 CPU under a v3 build fails, naming both levels: {fail}")
    check(cgf.cpu_line(3, 3).startswith("ok       CPU: x86-64-v3") and cgf.cpu_line(4, 3).startswith("ok ")
          and cgf.cpu_line(None, 3).startswith("ok       CPU: not checked") and cgf.cpu_line(1, 0) is None,
          "the level or more passes; not told passes; no level built (KZ_MARCH native) prints nothing")

    class Step:
        name, label = "check", "Checking your files"
    r = firstrun.Runner.__new__(firstrun.Runner)
    r.fail_inputs, r.cpu_failed = set(), False
    reasons = r.why_failed(Step(), [fail, "ok       EBOOT.elf"], 1)
    check(reasons == [" ".join(fail.split())] and r.cpu_failed and not r.fail_inputs,
          f"firstrun.py's check step passes the CPU line on and marks it (no input to ask again): {reasons}")
    env = dict(os.environ, KZ_MARCH="x86-64-v4")
    out = subprocess.run([sys.executable, str(ROOT / "tools" / "check-game-files.py"), "--quick", "--root", str(tmp),
                          "--work", str(tmp / "work")], capture_output=True, text=True, env=env).stdout
    check(any(ln.startswith(("ok       CPU:", "FAIL     CPU:")) and "x86-64-v4" in ln for ln in out.splitlines()),
          f"check-game-files.py --root prints the CPU line for KZ_MARCH's level: {out.splitlines()[:1]}")

print("test_place_checks:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
