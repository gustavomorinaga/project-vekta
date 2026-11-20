#!/usr/bin/env python3
"""Host test for the launcher contract (tools/release/launch.py, installer/linux/project-vekta, installer/*.cmd and
installer/linux/*.sh): the paths the launcher is given (--game, --eboot, --psarc, --rpcs3 [DIR]; KZ_GAME, KZ_EBOOT,
KZ_PSARC) become firstrun.py's options, and with nothing given or saved the first start looks for RPCS3 in its usual
places; the one exit-code table (port/kz1hd/kz_exit.h) is read whole, one code per cause with its text, and a failed
first run's step and reasons map onto it; --prepare prints only 'progress <0..1> <text>' lines and one 'reason <text>'
line on a failure, returns 0 when built, and its --timeout stops the run through firstrun.py's stop file; the entry
point uses no HOME, USER or XDG_RUNTIME_DIR, and the scripts in scripts/ find the folder above them. No game data."""
import io
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import launch  # noqa: E402
import rpcs3_import  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


class A:
    game = eboot = psarc = rpcs3 = None
    timeout = None


# The paths the launcher is given
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    a = A()
    check(launch.firstrun_args(str(root), a, {}) == ["--rpcs3"],
          "nothing given, nothing saved: RPCS3's usual places (no question on the first start)")
    check(launch.firstrun_args(str(root), a, {"KZ_GAME": "/g"}) == [],
          "KZ_GAME in the environment: firstrun.py reads it itself, no RPCS3 search")
    (root / "work").mkdir()
    (root / "work" / "paths.env").write_text("")
    check(launch.firstrun_args(str(root), a, {}) == [], "paths saved by an earlier run: used, no RPCS3 search")
    a.game, a.eboot, a.psarc = "my game", "e/EBOOT.elf", "d.psarc"
    got = launch.firstrun_args(str(root), a, {})
    check(got == ["--game", os.path.abspath("my game"), "--eboot", os.path.abspath("e/EBOOT.elf"), "--psarc",
                  os.path.abspath("d.psarc")], f"--game, --eboot, --psarc pass on as absolute paths: {got}")
    b = A()
    b.rpcs3 = ""
    c = A()
    c.rpcs3 = "rp"
    check(launch.firstrun_args(str(root), b, {}) == ["--rpcs3"] and
          launch.firstrun_args(str(root), c, {}) == ["--rpcs3", os.path.abspath("rp")], "--rpcs3 and --rpcs3 DIR")
    import firstrun
    check({n for n, *_ in firstrun.INPUTS} >= {"game", "eboot", "psarc"}, "firstrun.py takes the same three options")

# The exit-code table
E = launch.EXIT
codes = [c for c, _ in E.values()]
check(len(codes) == len(set(codes)) and all(t.strip().endswith(".") for _, t in E.values()),
      f"one code per cause, each with its one-line text: {sorted(codes)}")
need = {"NOT_PREPARED", "WRONG_EDITION", "FILE_MISSING", "NO_RPCS3", "BUILD_FAILED", "STOPPED", "CPU", "EBOOT",
        "NO_VULKAN", "USAGE"}
check(need <= set(E) and E["CPU"][0] == 75 and E["EBOOT"][0] == 76 and E["STOPPED"][0] == 130,
      "every cause of the plan is there; CPU 75, EBOOT 76, stopped 130 (firstrun.py's own)")
check(all(not 126 <= c <= 165 or c == 130 for c in codes) and 0 not in codes,
      "no code where a shell reports a signal (126-165), besides Ctrl+C's 130")
hdr = (ROOT / "port" / "kz1hd" / "kz_exit.h").read_text()
check(len(re.findall(r"^#define KZ_EXIT_\w+\s+\d+", hdr, re.M)) == len(E), "launch.py reads every #define of kz_exit.h")
for f, macro in (("cpu_check.c", "KZ_EXIT_CPU"), ("main.cpp", "KZ_EXIT_EBOOT")):
    check(macro in (ROOT / "port" / "kz1hd" / f).read_text(), f"{f} exits with {macro} from the same table")
H = rpcs3_import.HELP
cases = [("check", ["MISSING  EBOOT.elf (required): nothing at x"], 1, launch.FILE_MISSING),
         ("check", ["WRONG    EBOOT.elf: encrypted"], 1, launch.FILE_MISSING),
         ("check", ["DIFFERENT EBOOT.elf: SHA-256 1234..."], 1, launch.WRONG_EDITION),
         ("check", ["FAIL CPU: this build of Project Vekta needs a CPU with x86-64-v3"], 1, launch.EXIT_CPU),
         ("rpcs3", ["your RPCS3 cannot be used: " + H["not_found"]], 1, launch.NO_RPCS3),
         ("rpcs3", ["your RPCS3 cannot be used: " + H["no_exe"]], 1, launch.NO_RPCS3),
         ("rpcs3", ["your RPCS3 cannot be used: " + H["wrong_file"]], 1, launch.WRONG_EDITION),
         ("rpcs3", ["your RPCS3 cannot be used: " + H["no_game"]], 1, launch.FILE_MISSING),
         ("compile", ['the step "Compiling" failed (exit 1): error'], 1, launch.BUILD_FAILED),
         ("cancelled", [], 130, launch.STOPPED), ("", [], 2, E["USAGE"][0])]
for step, reasons, rc, want in cases:
    got = launch.classify(step, reasons, rc)
    check(got == want, f"a failed {step or 'run'} ({(reasons or [''])[0][:40]}...) gives {want}: {got}")
check(launch.classify("compile", [], 1, timed_out=True) == launch.STOPPED, "the time limit gives STOPPED")


# --prepare: firstrun.py's protocol turned into the launcher's lines
class FakeProc:
    def __init__(self, lines, rc, delay=0.0, stop=None):
        self.lines, self.rc, self.delay, self.stop, self.done = lines, rc, delay, stop, False

    @property
    def stdout(self):
        for ln in self.lines:
            if self.delay:
                t0 = time.monotonic()
                while time.monotonic() - t0 < self.delay and not (self.stop and os.path.exists(self.stop)):
                    time.sleep(0.01)
                if self.stop and os.path.exists(self.stop):
                    yield "RESULT ok=0 step=compile elapsed=1\n"
                    self.rc = 130
                    break
            yield ln + "\n"
        self.done = True

    def poll(self):
        return self.rc if self.done else None

    def wait(self):
        return self.rc


LINE = re.compile(r"^(progress (0|1)\.\d{3} \S.*|reason \S.*)$")
with tempfile.TemporaryDirectory() as tmp:
    seen = []

    def popen_ok(argv, **kw):
        seen.append((argv, kw))
        return FakeProc(["LOG /x/work/logs/firstrun.log", "checking...",
                         "PROGRESS step=0/8 name=rpcs3 done=1 total=2 elapsed=3 pct=0.0 file=EBOOT.BIN",
                         "PROGRESS step=1/8 name=check done=1 total=1 elapsed=4 pct=2.5",
                         "PROGRESS step=8/8 name=compile done=10 total=10 elapsed=900 pct=100.0",
                         "RESULT ok=1 step=- elapsed=901"], 0)
    out = io.StringIO()
    a = A()
    a.game = "g"
    rc = launch.prepare(tmp, a, out=out, popen=popen_ok)
    lines = out.getvalue().splitlines()
    check(rc == 0 and lines and all(LINE.match(ln) for ln in lines) and lines[-1] == "progress 1.000 Ready",
          f"a good run: only progress lines, ending at 1.000: {lines}")
    check(lines[0].startswith("progress 0.000 Making the decrypted copies with your RPCS3 (EBOOT.BIN)") and
          lines[1] == "progress 0.025 " + next(st.label for st in firstrun.STEPS if st.name == "check")
          if len(lines) > 1 else False,
          f"each line has the step's label (and the file RPCS3 is on): {lines[:2]}")
    argv, kw = seen[0]
    check("--game" in argv and "--stop-file" in argv and "--interactive" not in argv and "--bar" not in argv and
          kw.get("stdout") == subprocess.PIPE, "firstrun.py runs with the given paths, a stop file, no question or bar")

    def popen_fail(argv, **kw):
        return FakeProc(["PROGRESS step=1/8 name=check done=0 total=1 elapsed=1 pct=0.0",
                         "REASON MISSING EBOOT.elf (required): nothing at /x", "REASON MISSING PARAM.SFO (required)",
                         "RESULT ok=0 step=check elapsed=2"], 1)
    out = io.StringIO()
    rc = launch.prepare(tmp, A(), out=out, popen=popen_fail)
    lines = out.getvalue().splitlines()
    check(rc == launch.FILE_MISSING and all(LINE.match(ln) for ln in lines) and
          [ln for ln in lines if ln.startswith("reason ")] == ["reason MISSING EBOOT.elf (required): nothing at /x"] and
          lines[-1].startswith("reason "), f"a failed check: one reason line, at the end, and its code: {rc} {lines}")

    stop = os.path.join(tmp, "work", "logs", "prepare-stop")

    def popen_slow(argv, **kw):
        return FakeProc(["PROGRESS step=1/8 name=check done=1 total=1 elapsed=1 pct=2.5"] * 50, 0, delay=0.2, stop=stop)
    out = io.StringIO()
    a = A()
    a.timeout = 0.3
    t0 = time.monotonic()
    rc = launch.prepare(tmp, a, out=out, popen=popen_slow)
    lines = out.getvalue().splitlines()
    check(rc == launch.STOPPED and os.path.exists(stop) and time.monotonic() - t0 < 5 and
          lines[-1].startswith("reason stopped after 0.3 s (--timeout)"),
          f"--timeout writes the stop file (the run resumes later) and gives STOPPED: {rc} {lines[-1:]}")

# play without a build: NOT_PREPARED; no Vulkan loader: NO_VULKAN
with tempfile.TemporaryDirectory() as tmp:
    said = []
    real = launch.notify, launch.vulkan_present, launch.plan
    launch.notify = lambda text, error=True: said.append(text)
    try:
        check(launch.main(["--root", tmp]) == launch.NOT_PREPARED and launch.EXIT_TEXT[launch.NOT_PREPARED] in said[-1],
              "no build, no --build-if-needed: NOT_PREPARED with its text")
        launch.vulkan_present = lambda: False
        launch.plan = lambda root, fullscreen=False: (["true"], tmp, dict(os.environ, KZ_VFS=tmp, KZ_WORK=tmp),
                                                      os.path.join(tmp, "play.log"))
        os.environ.pop("KZ_VULKAN_CHECK", None)
        check(launch.main(["--root", tmp]) == launch.NO_VULKAN and launch.EXIT_TEXT[launch.NO_VULKAN] in said[-1],
              "no Vulkan loader: NO_VULKAN before the game starts")
    finally:
        launch.notify, launch.vulkan_present, launch.plan = real
check(isinstance(launch.vulkan_present(), bool), "the Vulkan check answers on this machine")

building = (ROOT / "docs" / "BUILDING.md").read_text()
check(all(f"| {c} | `{n}` | {t} |" in building for n, (c, t) in E.items()),
      "docs/BUILDING.md's exit-code table is kz_exit.h's, row for row")
trouble = (ROOT / "docs" / "TROUBLESHOOTING.md").read_text()
rows = re.findall(r"^\| [^|]+ \| (\d+) \| [^|]+ \| [^|]+ \|$", trouble, re.M)
check(sorted(map(int, rows)) == sorted(codes), f"docs/TROUBLESHOOTING.md has one row for each code: {sorted(rows)}")

# The entry point and the scripts
entry = (ROOT / "installer" / "linux" / "project-vekta").read_text()
code = "\n".join(ln for ln in entry.splitlines() if not ln.startswith("#"))
check(entry.startswith("#!/bin/sh\n") and not re.search(r"\$\{?(HOME|USER|XDG_RUNTIME_DIR)\b", code) and
      'cd "$here"' in code and "--build-if-needed" in code and os.access(ROOT / "installer/linux/project-vekta", os.X_OK),
      "project-vekta: executable, uses no HOME, USER or XDG_RUNTIME_DIR, runs from its folder, builds when needed")
for f in ("run.sh", "play.sh", "prepare.sh"):
    t = (ROOT / "installer" / "linux" / f).read_text()
    check('readlink -f "$0")")/.." && pwd)' in t, f"scripts/{f} finds the folder above it")
for f in ("run.cmd", "play.cmd", "prepare.cmd"):
    t = (ROOT / "installer" / f).read_text()
    check('cd /d "%~dp0.."' in t and "%~dp0..\\tools\\release\\" in t, f"scripts\\{f} finds the folder above it")
check("--prepare" in (ROOT / "installer" / "prepare.cmd").read_text() and
      "--prepare" in (ROOT / "installer" / "linux" / "prepare.sh").read_text(), "the prepare wrappers run --prepare")
with tempfile.TemporaryDirectory() as tmp:
    d = pathlib.Path(tmp) / "a folder"
    (d / "tools" / "release").mkdir(parents=True)
    (d / "tools" / "release" / "launch.py").write_text(
        "import os, sys\nprint(os.getcwd()); print(' '.join(sys.argv[1:])); print(os.environ.get('HOME', '-'))\n")
    (d / "project-vekta").write_text(entry)
    (d / "project-vekta").chmod(0o755)
    r = subprocess.run(["env", "-i", "PATH=/usr/bin:/bin", str(d / "project-vekta")], capture_output=True, text=True,
                       cwd="/")
    out = r.stdout.splitlines()
    check(r.returncode == 0 and out[:1] == [str(d)] and out[1:2] == [f"--root {d} --build-if-needed"] and
          out[2:3] == ["-"], f"started with no environment from another folder: runs launch.py from its own: {out}")

print("test_launcher_contract:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
