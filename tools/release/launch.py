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
--dry-run prints the command, the folders and the settings and starts nothing.

The Start menu's other entries (installer/vekta.iss [Icons]) run the launcher with one of these and start no game:
--repair ("Verify and rebuild"): the install's own files against manifest.json first (a missing or changed one means
"run the setup again over this folder"; nothing is rebuilt from a damaged install; a file CMake renamed to
<name>.included in the source tree counts as there), then, after a yes, firstrun.py --bar in a console of its own: the
check of your game files runs again, and every later step whose inputs changed (ninja rebuilds what changed). It ends
with a message that says how it went. --open-data ("Open saves folder"): the folder of the saves and settings in
Explorer. --support-package [--out DIR] ("Make a support package"): tools/release/support.py's zip, on the Desktop. --build-if-needed (the launcher, Project
Vekta.exe, passes it): before the first build, run.cmd opens in a console of its own, then the game starts if it
built.

After Project Pyrrhus's and Project Helghast's tools/release/launch.py (undecuple, MIT), which decide the same
things for their launchers. Exit: the game's own code, 1 = not built yet, 2 = usage. Standard library only.
"""
import argparse
import configparser
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import kzbuild  # noqa: E402
import support  # noqa: E402

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
    cwd = data_dir(root, e["KZ_VFS"], e)
    if cwd != e["KZ_VFS"]:
        # The game runs from the saves folder, so the folder it reads the game from is named: main.cpp takes it from
        # the EBOOT's path, but the raw file syscalls (sys_fs.c) and cellFs.c fall back to the working folder.
        e.setdefault("PS3_VFS_ROOT", e["KZ_VFS"].replace("\\", "/"))
    return [exe, eboot], cwd, e, log


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


def notify(text, error=True):
    """A message the player sees: stderr, or a message box under pythonw (the launcher's Python has no console)."""
    if sys.stderr is not None:
        print(f"launch: {text}", file=sys.stderr)
    elif kzbuild.WINDOWS:
        import ctypes
        ctypes.windll.user32.MessageBoxW(None, text, "Project Vekta", 0x10 if error else 0x40)


def ask(text):
    """Yes or no: a message box under pythonw, else a question on the console (no console: no)."""
    if sys.stdin is None or sys.stdout is None:
        if not kzbuild.WINDOWS:
            return False
        import ctypes
        return ctypes.windll.user32.MessageBoxW(None, text, "Project Vekta", 0x24) == 6   # MB_YESNO|MB_ICONQUESTION
    try:
        return input(f"{text} [y/N] ").strip().lower() in ("y", "yes")
    except EOFError:
        return False


def verify_install(root):
    """(files checked, [problems]): the install's own files against manifest.json's sizes and SHA-256."""
    try:
        with open(os.path.join(root, "manifest.json"), encoding="utf-8") as f:
            files = json.load(f)["files"]
    except (OSError, ValueError, KeyError):
        return 0, ["manifest.json (missing or unreadable)"]
    bad = []
    for e in files:
        p = os.path.join(root, *e["path"].split("/"))
        if not os.path.isfile(p):
            if not os.path.isfile(p + ".included"):   # zlib's CMake renames its zconf.h in the source tree
                bad.append(f"{e['path']} (missing)")
            continue
        h = hashlib.sha256()
        try:
            with open(p, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
        except OSError:
            bad.append(f"{e['path']} (unreadable)")
            continue
        if os.path.getsize(p) != e["size"] or h.hexdigest() != e["sha256"]:
            bad.append(f"{e['path']} (changed)")
    return len(files), bad


def console_python(root):
    """The Python with a console: the bundled python.exe in a release folder, else this one."""
    p = os.path.join(root, "work", "toolchain-cache", "python", "python.exe")
    return p if kzbuild.WINDOWS and os.path.isfile(p) else sys.executable


def status_reasons(path):
    """(the RESULT line, [REASON texts]) from firstrun.py's --status-file."""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return "", []
    result = next((ln for ln in lines if ln.startswith("RESULT ")), "")
    return result, [ln[7:] for ln in lines if ln.startswith("REASON ")]


def repair(root, run=subprocess.run):
    """--repair: verify the install, then check the game files again and rebuild what changed (docstring)."""
    checked, bad = verify_install(root)
    if bad:
        more = f"\n... and {len(bad) - 5} more" if len(bad) > 5 else ""
        notify(f"{len(bad)} of the {checked} files of this install are missing or changed:\n" + "\n".join(bad[:5]) +
               more + "\n\nRun the setup program again over this folder; your saves and settings stay.")
        return 1
    if not ask(f"The {checked} files of this install are intact.\n\nCheck your game files again and rebuild what "
               "changed? Steps whose inputs did not change are kept, so this takes from a minute to a full build; a "
               "console window shows the progress."):
        return 0
    status = os.path.join(root, "work", "logs", "repair-status.txt")
    os.makedirs(os.path.dirname(status), exist_ok=True)
    if os.path.exists(status):
        os.remove(status)
    rc = run([console_python(root), os.path.join(HERE, "firstrun.py"), "--root", root, "--bar", "--status-file",
              status], cwd=root, creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0)).returncode
    result, reasons = status_reasons(status)
    if rc == 0 and "ok=1" in result:
        notify("Verified: the install's files are intact, your game files pass the check and the port is up to "
               "date.", error=False)
        return 0
    why = "\n".join(reasons) or (result or f"exit {rc}")
    notify(f"The check or the build did not finish:\n\n{why}\n\nThe log is in {os.path.join(root, 'work', 'logs')}.")
    return rc or 1


def saves_folder(root, environ=None):
    e = kzbuild.resolve(environ, root=root)
    return data_dir(root, e["KZ_VFS"], e)


def open_folder(path, select=False):
    if kzbuild.WINDOWS:
        subprocess.Popen(["explorer.exe", f"/select,{path}"] if select else ["explorer.exe", path])
    elif sys.stdout is not None:
        print(path)


def open_data(root):
    """--open-data: the saves and settings folder in Explorer (made if it is not there yet)."""
    d = saves_folder(root)
    try:
        os.makedirs(d, exist_ok=True)
    except OSError as err:
        notify(f"cannot open {d}: {err}")
        return 1
    open_folder(d)
    return 0


def desktop(environ=None):
    environ = os.environ if environ is None else environ
    home = environ.get("USERPROFILE") or os.path.expanduser("~")
    d = os.path.join(home, "Desktop")
    return d if os.path.isdir(d) else home


def support_package(root, out="", environ=None):
    """--support-package: support.py's zip; says where it is."""
    import datetime
    import firstrun
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    try:
        dest, skipped = support.build(root, saves_folder(root, environ), out or desktop(environ), firstrun.redactor(),
                                      stamp)
    except OSError as err:
        notify(f"the support package could not be written: {err}")
        return 1
    if not dest:
        notify("Nothing to put in a support package yet: no logs and no settings. Start the game once, then try again.",
               error=False)
        return 1
    note = f"\n\nLeft out: {', '.join(skipped)}" if skipped else ""
    notify(f"The support package is ready:\n{dest}\n\nIt holds the logs and settings (your user folder is written as "
           f"~), not your saves or game files. Attach it to your bug report.{note}", error=False)
    open_folder(dest, select=True)
    return 0


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
    ap.add_argument("--repair", action="store_true", help='"Verify and rebuild": check the install, then rebuild')
    ap.add_argument("--open-data", action="store_true", help='"Open saves folder"')
    ap.add_argument("--support-package", action="store_true", help='"Make a support package": a zip of the logs')
    ap.add_argument("--out", default="", help="with --support-package: the folder for the zip (default: the Desktop)")
    a = ap.parse_args(argv)
    root = os.path.abspath(a.root)
    if a.repair:
        return repair(root)
    if a.open_data:
        return open_data(root)
    if a.support_package:
        return support_package(root, a.out)
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
        for k, _ in SETTINGS + FIXED + [("KZ_LIBSRE_IMAGE", ""), ("PS3RECOMP_VK_DISPLAY", ""), ("KZ_LOG", ""),
                                        ("PS3_VFS_ROOT", "")]:
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
