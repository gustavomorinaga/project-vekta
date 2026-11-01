#!/usr/bin/env python3
"""Start the game from a built tree: the release folder's play.cmd (and the launcher) run this on Windows; on Linux
tools/play.sh does the same and stays the documented way.

    tools/release/launch.py [--root DIR] [--dry-run] [--fullscreen]

The paths are tools/kzbuild.py's (tools/env.sh's): the build (KZ_BIN, default work/build-win/KZ1HD.exe on Windows,
work/build/KZ1HD elsewhere), the run folder (KZ_VFS, default work/vfs) and the firmware's libsre image
(work/fw/out/libsre_image.bin). The game runs from the run folder with tools/play.sh's settings (each one a default:
the environment wins) and its log goes to work/play.log. When the window closes the game is stopped, as play.sh
does. Without a build or a run folder it says which step is missing (run.cmd, or tools/install.sh).
--dry-run prints the command, the folder and the settings and starts nothing.

After Project Pyrrhus's and Project Helghast's tools/release/launch.py (undecuple, MIT), which decide the same
things for their launchers. Exit: the game's own code, 1 = not built yet, 2 = usage. Standard library only.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import kzbuild  # noqa: E402

# tools/play.sh's settings (docs there): the environment wins over each.
SETTINGS = [
    ("GCM_PARK_SKIP", "1"), ("PS3RECOMP_VK_GUEST_PROGRAMS", "1"), ("PS3_VERBOSE", "0"), ("KZ_STATS", "1"),
    ("PS3_GCM_MAX_QUEUED_FLIPS", "1"),
]
FIXED = [("PS3RECOMP_VK_WINDOW", "1"), ("PS3_TITLE", "Killzone HD")]
CLOSED = (b"window closed", b"window disabled")


def plan(root, environ=None, fullscreen=False):
    """(argv, cwd, env, log path); ValueError names the missing step."""
    e = kzbuild.resolve(environ, root=root)
    w = e["KZ_WORK"]
    exe = e.get("KZ_BIN") or (os.path.join(w, "build-win", "KZ1HD.exe") if kzbuild.WINDOWS
                              else os.path.join(w, "build", "KZ1HD"))
    first = "run.cmd" if kzbuild.WINDOWS else "tools/install.sh"
    eboot = os.path.join(e["KZ_VFS"], "PS3_GAME", "USRDIR", "EBOOT.elf")
    if not os.path.isfile(eboot):
        raise ValueError(f"no game folder at {e['KZ_VFS']} yet: run {first} first")
    if not os.path.isfile(exe):
        raise ValueError(f"no build at {exe} yet: run {first} first")
    for k, v in SETTINGS:
        e.setdefault(k, v)
    e.setdefault("KZ_LIBSRE_IMAGE", os.path.join(w, "fw", "out", "libsre_image.bin"))
    for k, v in FIXED:
        e[k] = v
    if fullscreen or e.get("KZ_FULLSCREEN") == "1":
        e["PS3RECOMP_VK_DISPLAY"] = "borderless"
    log = os.path.join(w, "play.log")
    if kzbuild.WINDOWS:
        e.setdefault("KZ_LOG", log)   # the exe is a GUI program: it writes its own log there
    return [exe, eboot], e["KZ_VFS"], e, log


def window_closed(log):
    try:
        with open(log, "rb") as f:
            data = f.read()
    except OSError:
        return False
    return any(c in data for c in CLOSED)


def run(argv, cwd, env, log, poll=1.0):
    """Runs the game; its output goes to log (on Windows the exe writes KZ_LOG itself)."""
    own = env.get("KZ_LOG") == log
    with open(os.devnull if own else log, "wb") as out:
        p = subprocess.Popen(argv, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT)
        # The window closing leaves the process running headless: stop it then.
        while p.poll() is None:
            if window_closed(log):
                p.kill()
                break
            time.sleep(poll)
        return p.wait()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(HERE)))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--fullscreen", action="store_true")
    a = ap.parse_args(argv)
    try:
        argv_, cwd, env, log = plan(os.path.abspath(a.root), fullscreen=a.fullscreen)
    except ValueError as err:
        print(f"launch: {err}", file=sys.stderr)
        return 1
    if a.dry_run:
        print("run:", " ".join(f'"{x}"' if " " in x else x for x in argv_))
        print("in:", cwd)
        print("log:", log)
        for k, _ in SETTINGS + FIXED + [("KZ_LIBSRE_IMAGE", ""), ("PS3RECOMP_VK_DISPLAY", ""), ("KZ_LOG", "")]:
            if k in env:
                print(f"{k}={env[k]}")
        return 0
    rc = run(argv_, cwd, env, log)
    print(f"log: {log}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
