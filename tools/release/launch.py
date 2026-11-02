#!/usr/bin/env python3
"""Start the game from a built tree: the release folder's play.cmd (and the launcher) run this on Windows; on Linux
tools/play.sh does the same and stays the documented way.

    tools/release/launch.py [--root DIR] [--dry-run] [--fullscreen]

The paths are tools/kzbuild.py's (tools/env.sh's): the build (KZ_BIN, default work/build-win/KZ1HD.exe on Windows,
work/build/KZ1HD elsewhere), the run folder (KZ_VFS, default work/vfs) and the firmware's libsre image
(work/fw/out/libsre_image.bin). The game runs from the run folder with tools/play.sh's settings (each one a default:
the environment wins) and its log goes to work/play.log. When the window closes the game is stopped, as play.sh
does. Without a build or a run folder it says which step is missing (run.cmd, or tools/install.sh).
Where the saves and settings go (kz1hd_pc.ini, kz1hd_keys.ini, gamedata/): the game runs from that folder. In a
copy the setup program installed (vekta-install.ini next to run.cmd) it is %APPDATA%/Project Vekta, unless the install
is portable ([install] portable=1): then it is the run folder, as in a Git clone or the Windows zip. Saves an earlier
version left in the run folder are moved there once (migrate), never over a file already there.
--dry-run prints the command, the folders and the settings and starts nothing. --build-if-needed (the launcher, Project
Vekta.exe, passes it): before the first build, run.cmd opens in a console of its own, then the game starts if it
built.

After Project Pyrrhus's and Project Helghast's tools/release/launch.py (undecuple, MIT), which decide the same
things for their launchers. Exit: the game's own code, 1 = not built yet, 2 = usage. Standard library only.
"""
import argparse
import configparser
import os
import shutil
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
INSTALL_INI = "vekta-install.ini"   # the setup program writes it in the install folder ([INI] in installer/vekta.iss)
SAVE_FOLDER = "Project Vekta"       # under %APPDATA% when the install is not portable
PLAYER_FILES = ("kz1hd_pc.ini", "kz1hd_keys.ini", "gamedata")   # what the game writes in the folder it runs from


def install_settings(root):
    """vekta-install.ini's settings, or None in a folder the setup program did not install. A file that cannot be
    read counts as portable: the saves stay where they are."""
    path = os.path.join(root, INSTALL_INI)
    if not os.path.isfile(path):
        return None
    c = configparser.ConfigParser(interpolation=None)
    try:
        c.read(path, encoding="utf-8-sig")
    except (configparser.Error, OSError, UnicodeError):
        return {"portable": True}
    return {"portable": c.get("install", "portable", fallback="1").strip() != "0"}


def data_dir(root, run_folder, environ):
    """The folder the game runs from, which holds the saves and settings (see the docstring)."""
    ini = install_settings(root)
    appdata = environ.get("APPDATA", "")
    if ini is None or ini["portable"] or not appdata:
        return run_folder
    return os.path.join(appdata, SAVE_FOLDER)


def migrate(run_folder, data):
    """Moves the saves and settings an earlier version left in the run folder to data; returns what moved."""
    moved = []
    if os.path.normcase(os.path.abspath(run_folder)) == os.path.normcase(os.path.abspath(data)):
        return moved
    for name in PLAYER_FILES:
        src, dst = os.path.join(run_folder, name), os.path.join(data, name)
        if os.path.lexists(src) and not os.path.lexists(dst):
            os.makedirs(data, exist_ok=True)
            shutil.move(src, dst)
            moved.append(name)
    return moved


def plan(root, environ=None, fullscreen=False):
    """(argv, cwd, env, log path); ValueError names the missing step. cwd is data_dir's folder."""
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
    # The game finds its files from the EBOOT's path (main.cpp derive_vfs_root), so it may run from elsewhere.
    return [exe, eboot], data_dir(root, e["KZ_VFS"], e), e, log


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


def notify(text):
    """A message the player sees: stderr, or a message box under pythonw (the launcher's Python has no console)."""
    if sys.stderr is not None:
        print(f"launch: {text}", file=sys.stderr)
    elif kzbuild.WINDOWS:
        import ctypes
        ctypes.windll.user32.MessageBoxW(None, text, "Project Vekta", 0x10)


def first_run(root):
    """run.cmd (the questions and the build) in a console of its own; its exit code, or None without it."""
    cmd = os.path.join(root, "run.cmd")
    if not os.path.isfile(cmd):
        return None
    return subprocess.run(["cmd.exe", "/c", cmd], cwd=root,
                          creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0)).returncode


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(HERE)))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--fullscreen", action="store_true")
    ap.add_argument("--build-if-needed", action="store_true",
                    help="before the first build, open run.cmd (Windows release folder; the launcher passes it)")
    a = ap.parse_args(argv)
    root = os.path.abspath(a.root)
    try:
        argv_, cwd, env, log = plan(root, fullscreen=a.fullscreen)
    except ValueError as err:
        if not (a.build_if_needed and not a.dry_run and first_run(root) is not None):
            notify(str(err))
            return 1
        try:   # the first run is done (or stopped): play if it built
            argv_, cwd, env, log = plan(root, fullscreen=a.fullscreen)
        except ValueError:
            return 1
    if a.dry_run:
        print("run:", " ".join(f'"{x}"' if " " in x else x for x in argv_))
        print("in:", cwd, "(saves and settings)")
        print("log:", log)
        for k, _ in SETTINGS + FIXED + [("KZ_LIBSRE_IMAGE", ""), ("PS3RECOMP_VK_DISPLAY", ""), ("KZ_LOG", "")]:
            if k in env:
                print(f"{k}={env[k]}")
        return 0
    try:
        os.makedirs(cwd, exist_ok=True)
        moved = migrate(env["KZ_VFS"], cwd)
    except OSError as err:
        notify(f"cannot use {cwd} for the saves: {err}")
        return 1
    if moved and sys.stdout is not None:
        print(f"launch: moved {', '.join(moved)} to {cwd}")
    rc = run(argv_, cwd, env, log)
    if sys.stdout is not None:
        print(f"log: {log}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
