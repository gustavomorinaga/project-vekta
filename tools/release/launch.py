#!/usr/bin/env python3
"""Start the game from a built tree: the release folder's play.cmd (and the launcher) run this on Windows; on Linux
tools/play.sh does the same and stays the documented way.

    tools/release/launch.py [--root DIR] [--dry-run] [--fullscreen]

The paths are tools/kzbuild.py's (tools/env.sh's): the build (KZ_BIN, default work/build-win/KZ1HD.exe on Windows,
work/build/KZ1HD elsewhere) and the run folder (KZ_VFS, default work/vfs). The game runs from the run folder with tools/play.sh's settings (each one a default:
the environment wins) and its log goes to work/play.log. When the window closes the game is stopped, as play.sh
does. Without a build or a run folder it says which step is missing (run.cmd, or tools/install.sh).
Where the saves and settings go (kz1hd_pc.ini, kz1hd_keys.ini, gamedata/): the game runs from that folder. In a
copy the setup program installed (vekta-install.ini next to run.cmd) it is %APPDATA%/Project Vekta, unless the install
is portable ([install] portable=1): then it is the run folder, as in a Git clone or the Windows zip. Saves an earlier
version left in the run folder are moved there once (migrate), never over a file already there.
--dry-run prints the command, the folders and the settings and starts nothing.
Before the game starts, tools/release/version_check.py's daily check runs when the player turned it on (off by
default; one anonymous request to GitHub at most once a day) and a newer release is named in a message.
--move-data appdata|portable (a setup install only): moves the saves and settings to %APPDATA%/Project Vekta or back
into the run folder, and records the choice in vekta-install.ini, so a choice made in the setup can be changed later.
Nothing is overwritten: when the other folder already holds one of those files, nothing moves and it says which.
After Project Helghast's launch.py switch_data (undecuple, MIT); written here.

The Start menu's other entries (installer/vekta.iss [Icons]) run the launcher with one of these and start no game:
--repair ("Verify and rebuild"): the install's own files against manifest.json first (a missing or changed one means
"run the setup again over this folder"; nothing is rebuilt from a damaged install; a file CMake renamed to
<name>.included in the source tree counts as there), then, after a yes, firstrun.py --bar in a console of its own: the
check of your game files runs again, and every later step whose inputs changed (ninja rebuilds what changed). It ends
with a message that says how it went. --open-data ("Open saves folder"): the folder of the saves and settings in
Explorer. --support-package [--out DIR] ("Make a support package"): tools/release/support.py's zip, on the Desktop. --build-if-needed (the launcher, Project
Vekta.exe, passes it): before the first build, run.cmd opens in a console of its own, then the game starts if it
built. With a build there that this install's files did not make (stale_build: its build-stamp.json names another
version, patch set or DLSS choice, as after an install over an earlier release without the build in the setup),
firstrun.py --bar rebuilds it first in a console of its own (run.cmd when no paths were saved yet), so the game of
an earlier version never starts from the new install. A game that ends within EARLY_EXIT seconds without its window
being closed or the player quitting it did not start: a message then says so, with the end of its log. One of the
exe's own exit codes (EXE_LINES, port/kz1hd/kz_exit.h: a CPU below the level the build is made for, an EBOOT.elf
other than the one the build was lifted from) gets its own message instead; the EBOOT's comes with an offer to rebuild
(as a stale build's), and the game starts once that build is done.

After Project Pyrrhus's and Project Helghast's tools/release/launch.py (undecuple, MIT), which decide the same
things for their launchers. Exit: the game's own code, 1 = not built yet, 2 = usage. Standard library only.
"""
import argparse
import configparser
import hashlib
import json
import os
import re
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
# The game's own "[kz] quitting (<why>)" line (main.cpp kz_quit: the window closed, Quit Game, a signal) means the
# player ended it on purpose, however soon: Quit Game from the main menu within seconds is not a failed start.
CLOSED = (b"window closed", b"window disabled", b"[kz] quitting (")
INSTALL_INI = "vekta-install.ini"   # the setup program writes it in the install folder ([INI] in installer/vekta.iss)
SAVE_FOLDER = "Project Vekta"       # under %APPDATA% when the install is not portable
PLAYER_FILES = ("kz1hd_pc.ini", "kz1hd_keys.ini", "gamedata")   # what the game writes in the folder it runs from
# The exit codes, the one table (port/kz1hd/kz_exit.h: KZ1HD's own and this launcher's, each with the line the player
# is told). Read from there, so the exe, this file, the docs and recomplauncher.json never disagree.
KZ_EXIT_H = os.path.join(os.path.dirname(os.path.dirname(HERE)), "port", "kz1hd", "kz_exit.h")


def exit_codes(path=KZ_EXIT_H):
    """{name: (code, fix text)} from kz_exit.h's '#define KZ_EXIT_<NAME> <code> /* <text> */' lines."""
    with open(path, encoding="utf-8") as f:
        return {m.group(1): (int(m.group(2)), m.group(3)) for m in
                re.finditer(r"^#define KZ_EXIT_(\w+)\s+(\d+)\s*/\*\s*(.*?)\s*\*/", f.read(), re.M)}


EXIT = exit_codes()
EXIT_TEXT = {code: text for code, text in EXIT.values()}
EXIT_CPU, EXIT_EBOOT = EXIT["CPU"][0], EXIT["EBOOT"][0]
NOT_PREPARED, WRONG_EDITION, FILE_MISSING, NO_RPCS3, BUILD_FAILED, NO_VULKAN, STOPPED = (
    EXIT[n][0] for n in ("NOT_PREPARED", "WRONG_EDITION", "FILE_MISSING", "NO_RPCS3", "BUILD_FAILED", "NO_VULKAN",
                         "STOPPED"))
# KZ1HD writes its own line for these into the log: (the start of that line, the part of it not shown)
EXE_LINES = {EXIT_CPU: ("[kz] CPU check: ", "[kz] CPU check: "),
             EXIT_EBOOT: ("[kz] EBOOT check: This build", "[kz] EBOOT check: "),
             NO_VULKAN: ("[kz] Vulkan check: ", "[kz] Vulkan check: ")}
EARLY_EXIT = 20                     # seconds: a game that ends sooner, its window not closed, did not start


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


def move_data(root, to, environ=None):
    """--move-data: (ok, message). to is "appdata" or "portable"; see the docstring."""
    environ = os.environ if environ is None else environ
    ini = install_settings(root)
    if ini is None:
        return False, "only an install made by the setup program keeps its saves apart; here they are in the run folder"
    run_folder = kzbuild.resolve(environ, root=root)["KZ_VFS"]
    old = data_dir(root, run_folder, environ)
    appdata = environ.get("APPDATA", "")
    if to == "appdata" and not appdata:
        return False, "there is no %APPDATA% folder here"
    new = os.path.join(appdata, SAVE_FOLDER) if to == "appdata" else run_folder
    if os.path.normcase(os.path.abspath(old)) != os.path.normcase(os.path.abspath(new)):
        clash = [n for n in PLAYER_FILES if os.path.lexists(os.path.join(old, n)) and
                 os.path.lexists(os.path.join(new, n))]
        if clash:
            return False, (f"{new} already has {', '.join(clash)}: nothing was moved. Move or remove them there "
                           "first, so no save is overwritten")
        os.makedirs(new, exist_ok=True)
        for n in PLAYER_FILES:
            if os.path.lexists(os.path.join(old, n)):
                shutil.move(os.path.join(old, n), os.path.join(new, n))
    c = configparser.ConfigParser(interpolation=None)
    path = os.path.join(root, INSTALL_INI)
    try:
        c.read(path, encoding="utf-8-sig")
    except (configparser.Error, OSError, UnicodeError):
        pass
    if not c.has_section("install"):
        c.add_section("install")
    c.set("install", "portable", "1" if to == "portable" else "0")
    with open(path, "w", encoding="utf-8") as f:
        c.write(f)
    return True, f"the saves and settings are now in {new}"


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
    if own:
        # The exe empties its log only once it runs: until then the file holds the last session, whose "window
        # closed" would stop this one at once (an install over an earlier version keeps that log). Empty it first.
        open(log, "wb").close()
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


def rebuild(root, run=subprocess.run):
    """firstrun.py --bar in a console of its own: (exit code, the RESULT line, [REASON texts])."""
    status = os.path.join(root, "work", "logs", "repair-status.txt")
    os.makedirs(os.path.dirname(status), exist_ok=True)
    if os.path.exists(status):
        os.remove(status)
    rc = run([console_python(root), os.path.join(HERE, "firstrun.py"), "--root", root, "--bar", "--status-file",
              status], cwd=root, creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0)).returncode
    result, reasons = status_reasons(status)
    return rc, result, reasons


def stale_build(root):
    """Why the build in this release folder is not the one its files make, or None (see the docstring). Only a
    folder that ships the patched ps3recomp is checked: a Git clone builds with tools/build.sh, which writes no
    stamp."""
    import firstrun
    w = os.path.join(root, "work")
    patchset = firstrun.read_patchset(w)
    if patchset is None:
        return None
    stamp = firstrun.read_stamp(w)
    if stamp is None:
        return "the build has no stamp"
    for key, want in (("version", firstrun.read_version(root)), ("patchset", patchset)):
        if stamp.get(key) != want:
            return f"the build is {key} {stamp.get(key)}, this install {want}"
    choice = firstrun.read_dlss(w)
    if choice and stamp.get("dlss") != (choice == "accept"):
        return f"DLSS was {'built in' if stamp.get('dlss') else 'left out'}, the choice is now {choice}"
    return None


def update_build(root, run=subprocess.run):
    """--build-if-needed with a stale build: rebuild it; True when the game may start."""
    if not os.path.isfile(os.path.join(root, "work", "paths.env")):
        return first_run(root) == 0
    rc, result, reasons = rebuild(root, run)
    if rc == 0 and "ok=1" in result:
        return True
    why = "\n".join(reasons) or (result or f"exit {rc}")
    notify(f"Project Vekta was updated, and the build for the new version did not finish:\n\n{why}\n\n"
           f"The log is in {os.path.join(root, 'work', 'logs')}. Start Project Vekta again after a fix: the build "
           "resumes where it stopped.")
    return False


def log_tail(log, lines=12, chars=1500):
    try:
        with open(log, encoding="utf-8", errors="replace") as f:
            text = [ln.rstrip() for ln in f.read().splitlines() if ln.strip()]
    except OSError:
        return ""
    return "\n".join(text[-lines:])[-chars:]


def exit_code_text(rc, log):
    """The message for one of KZ1HD's own exit codes (EXE_LINES): the exe's own line from the log, else kz_exit.h's
    text; None for any other code."""
    if rc not in EXE_LINES:
        return None
    (start, strip), text = EXE_LINES[rc], EXIT_TEXT[rc]
    try:
        with open(log, encoding="utf-8", errors="replace") as f:
            text = next((ln[len(strip):].strip() for ln in f if ln.startswith(start)), text)
    except OSError:
        pass
    return text


def early_exit_text(rc, log):
    tail = log_tail(log)
    return (f"The game stopped right after it started (exit code {rc}).\n\n" +
            (f"The end of its log:\n{tail}\n\n" if tail else "It wrote no log.\n\n") +
            f"The whole log: {log}\nFor a bug report: the Start menu's \"Make a support package\".")


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
    rc, result, reasons = rebuild(root, run)
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


INPUTS = (("game", "KZ_GAME"), ("eboot", "KZ_EBOOT"), ("psarc", "KZ_PSARC"))


def firstrun_args(root, a, environ=None):
    """firstrun.py's options for what the launcher was given: --game, --eboot, --psarc (the KZ_* variables are read by
    firstrun.py itself, and all of them are saved in work/paths.env once the check passes) and --rpcs3 [DIR]. With
    nothing given or saved, --rpcs3: RPCS3 and the game in their usual places, so that a first start asks nothing.
    A game folder without a decrypted EBOOT (--game, no --eboot nor KZ_EBOOT) is the folder RPCS3 installed, with
    its encrypted EBOOT.BIN: it is also the hint of --rpcs3, so that the player's RPCS3 makes the decrypted copy (a
    launcher such as RecompLauncher passes only the game folder). --disc PATH (the disc with no RPCS3 installed,
    rpcs3_disc.py) is passed on as it is and wins over the RPCS3 route."""
    environ = os.environ if environ is None else environ
    out = []
    for name, _ in INPUTS:
        if getattr(a, name, None):
            out += ["--" + name, os.path.abspath(getattr(a, name))]
    given = bool(out)
    rpcs3 = getattr(a, "rpcs3", None)
    game = getattr(a, "game", None)
    if getattr(a, "disc", None):
        out += ["--disc", os.path.abspath(a.disc)]
    elif rpcs3 is not None:
        out += ["--rpcs3"] + ([os.path.abspath(rpcs3)] if rpcs3 else [])
    elif game and not getattr(a, "eboot", None) and not environ.get("KZ_EBOOT"):
        out += ["--rpcs3", os.path.abspath(game)]
    elif not given and not any(environ.get(v) for _, v in INPUTS) and \
            not os.path.isfile(os.path.join(root, "work", "paths.env")):
        out.append("--rpcs3")
    if getattr(a, "jobs", None):
        out += ["--jobs", str(a.jobs)]
    if getattr(a, "dlss", None):   # the player's answer to NVIDIA's DLSS SDK licence (recomplauncher.json's tick)
        out += ["--dlss", a.dlss]
    return out


def firstrun_env(root):
    """The environment firstrun.py builds in: the Linux release folder's pinned toolchain when it is there."""
    e = dict(os.environ)
    if not kzbuild.WINDOWS and os.path.isdir(os.path.join(root, "work", "toolchain-cache", "zig")):
        e["KZ_LINUX_TOOLCHAIN"] = "pinned"
    return e


def classify(step, reasons, rc, timed_out=False):
    """The exit code (kz_exit.h) for a first run that did not finish: firstrun.py's failing step and REASON lines."""
    import rpcs3_disc
    import rpcs3_import
    text = "\n".join(reasons)
    if timed_out or rc == 130 or step == "cancelled":
        return STOPPED
    if rc == 2:
        return EXIT["USAGE"][0]
    if "CPU: " in text:
        return EXIT_CPU
    if any(rpcs3_import.HELP[k][:40] in text for k in ("not_found", "not_rpcs3", "no_exe")):
        return NO_RPCS3
    if "DIFFERENT" in text or rpcs3_import.HELP["wrong_file"][:40] in text or \
            any(rpcs3_disc.HELP[k][:40] in text for k in ("not_pkg", "psn_pkg", "bad_pkg")):
        return WRONG_EDITION
    if step in ("check", "rpcs3", "disc"):
        return FILE_MISSING
    return BUILD_FAILED


def prepare(root, a, out=None, popen=subprocess.Popen, poll=1.0):
    """--prepare: firstrun.py without a window or a question. Prints 'progress <0..1> <text>' lines, and on a
    failure one 'reason <text>' line at the end; returns 0 when built, else kz_exit.h's code. --timeout SECS stops
    the run cleanly (firstrun.py's stop file; the next run resumes) and returns STOPPED."""
    import firstrun
    import threading
    out = out or sys.stdout
    labels = {st.name: st.label for st in firstrun.STEPS}
    labels["rpcs3"] = "Making the decrypted copies with your RPCS3"
    labels["disc"] = "Installing the game from your disc with the official RPCS3"
    stop = os.path.join(root, "work", "logs", "prepare-stop")
    os.makedirs(os.path.dirname(stop), exist_ok=True)
    if os.path.exists(stop):
        os.remove(stop)
    argv = [console_python(root), os.path.join(HERE, "firstrun.py"), "--root", root, "--stop-file", stop,
            *firstrun_args(root, a)]
    p = popen(argv, cwd=root, env=firstrun_env(root), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
              encoding="utf-8", errors="replace", creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    timed = threading.Event()
    if getattr(a, "timeout", None):
        def stop_later():
            if p.poll() is None:
                timed.set()
                open(stop, "w").close()
        threading.Timer(a.timeout, stop_later).start()
    ok, step, reasons = False, "", []
    for line in p.stdout:
        line = line.rstrip("\n")
        if line.startswith("PROGRESS "):
            f = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
            label = labels.get(f.get("name"), f.get("name", ""))
            if f.get("file"):
                label += f" ({f['file']})"
            print(f"progress {min(1.0, float(f.get('pct', 0)) / 100):.3f} {label}", file=out, flush=True)
        elif line.startswith("REASON "):
            reasons.append(line[7:])
        elif line.startswith("RESULT "):
            f = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
            ok, step = f.get("ok") == "1", f.get("step", "")
    rc = p.wait()
    if ok and rc == 0:
        print("progress 1.000 Ready", file=out, flush=True)
        return 0
    code = classify(step, reasons, rc, timed.is_set())
    why = (f"stopped after {a.timeout} s (--timeout): " + EXIT_TEXT[code]) if timed.is_set() else \
        (reasons[0] if reasons else EXIT_TEXT[code])
    print(f"reason {' '.join(why.split())}", file=out, flush=True)
    return code


def first_run(root, a):
    """Before the first build (--build-if-needed): firstrun.py --bar in a console of its own (on Windows; on Linux
    in this terminal) with what the launcher was given, else RPCS3's usual places, asking nothing. When that finds no
    RPCS3 and nothing was given, the first run's questions (scripts/run.cmd, scripts/run.sh) where a console can show
    them. Returns 0 when built, else kz_exit.h's code; the reason is told."""
    args = firstrun_args(root, a)
    status = os.path.join(root, "work", "logs", "first-status.txt")
    os.makedirs(os.path.dirname(status), exist_ok=True)
    if os.path.exists(status):
        os.remove(status)
    new_console = getattr(subprocess, "CREATE_NEW_CONSOLE", 0)
    rc = subprocess.run([console_python(root), os.path.join(HERE, "firstrun.py"), "--root", root, "--bar",
                         "--status-file", status, *args], cwd=root, env=firstrun_env(root),
                        creationflags=new_console).returncode
    result, reasons = status_reasons(status)
    if rc == 0 and "ok=1" in result:
        return 0
    step = re.search(r"step=(\S+)", result)
    code = classify(step.group(1) if step else "", reasons, rc)
    ask_here = kzbuild.WINDOWS or (sys.stdin is not None and sys.stdin.isatty())
    script = os.path.join(root, "scripts", "run.cmd" if kzbuild.WINDOWS else "run.sh")
    if code == NO_RPCS3 and args == ["--rpcs3"] and ask_here and os.path.isfile(script):
        argv = ["cmd.exe", "/c", script] if kzbuild.WINDOWS else ["sh", script]
        rc = subprocess.run(argv, cwd=root, creationflags=new_console).returncode
        return 0 if rc == 0 else (STOPPED if rc == 130 else NOT_PREPARED)
    notify("\n".join([EXIT_TEXT[code]] + reasons[:3]))
    return code


def vulkan_present():
    """Whether a Vulkan loader can be opened (vulkan-1.dll, libvulkan.so.1): without one the game cannot draw."""
    import ctypes
    try:
        (ctypes.WinDLL if kzbuild.WINDOWS else ctypes.CDLL)("vulkan-1.dll" if kzbuild.WINDOWS else "libvulkan.so.1")
        return True
    except OSError:
        return False


def parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(HERE)))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--fullscreen", action="store_true")
    ap.add_argument("--build-if-needed", action="store_true",
                    help="before the first build, check the files and build (the launchers Project Vekta.exe and "
                         "project-vekta pass it)")
    ap.add_argument("--prepare", action="store_true",
                    help="check the files and build without a window or a question: 'progress <0..1> <text>' lines, "
                         "a 'reason <text>' line on a failure, exit 0 when built (else kz_exit.h's code)")
    ap.add_argument("--timeout", type=float, metavar="SECS", help="with --prepare: stop after SECS (it resumes)")
    ap.add_argument("--jobs", type=int, metavar="N",
                    help="parallel build jobs (default: KZ_JOBS, else from free memory); fewer for a PC that gets "
                         "too hot or switches off while compiling")
    for name, var in INPUTS:
        ap.add_argument("--" + name, metavar="DIR" if name == "game" else "FILE",
                        help=f"your {name} ({var}; kept in work/paths.env)")
    ap.add_argument("--rpcs3", nargs="?", const="", metavar="DIR",
                    help="take the game from your RPCS3 (DIR, else its usual places)")
    ap.add_argument("--disc", metavar="PATH",
                    help="install the game from your Killzone Trilogy disc (its folder, or PS3_EXTRA's package) with "
                         "the official RPCS3, downloaded for this (no RPCS3 of yours needed)")
    ap.add_argument("--dlss", choices=("accept", "decline"),
                    help="accept: you accept NVIDIA's DLSS SDK licence and the build downloads it (firstrun.py --dlss)")
    ap.add_argument("--repair", action="store_true", help='"Verify and rebuild": check the install, then rebuild')
    ap.add_argument("--open-data", action="store_true", help='"Open saves folder"')
    ap.add_argument("--support-package", action="store_true", help='"Make a support package": a zip of the logs')
    ap.add_argument("--out", default="", help="with --support-package: the folder for the zip (default: the Desktop)")
    ap.add_argument("--move-data", choices=("appdata", "portable"),
                    help="move the saves and settings to %%APPDATA%%\\Project Vekta or into the install folder")
    return ap


def main(argv=None):
    ap = parser()
    a = ap.parse_args(argv)
    root = os.path.abspath(a.root)
    if a.prepare:
        return prepare(root, a)
    if a.repair:
        return repair(root)
    if a.open_data:
        return open_data(root)
    if a.support_package:
        return support_package(root, a.out)
    if a.move_data:
        ok, text = move_data(root, a.move_data)
        notify(text, error=not ok)
        return 0 if ok else 1
    try:
        argv_, cwd, env, log = plan(root, fullscreen=a.fullscreen)
    except ValueError as err:
        if not (a.build_if_needed and not a.dry_run):
            notify(f"{err}\n\n{EXIT_TEXT[NOT_PREPARED]}")
            return NOT_PREPARED
        rc = first_run(root, a)
        if rc:
            return rc
        try:   # the first run built it: play
            argv_, cwd, env, log = plan(root, fullscreen=a.fullscreen)
        except ValueError as err2:
            notify(str(err2))
            return NOT_PREPARED
    else:
        why = stale_build(root) if a.build_if_needed and not a.dry_run else None
        if why:
            if sys.stdout is not None:
                print(f"launch: rebuilding first: {why}")
            if not update_build(root):
                return 1
    if a.dry_run:
        print("run:", " ".join(f'"{x}"' if " " in x else x for x in argv_))
        print("in:", cwd, "(saves and settings)")
        print("log:", log)
        for k, _ in SETTINGS + FIXED + [("PS3RECOMP_VK_DISPLAY", ""), ("KZ_LOG", ""),
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
    import version_check   # off unless turned on (version_check.py; README "What goes online")
    news = version_check.check(root, env["KZ_WORK"], env)
    if news:
        notify(news, error=False)
    if os.environ.get("KZ_VULKAN_CHECK", "1") != "0" and not vulkan_present():   # KZ_VULKAN_CHECK=0 skips it
        notify(EXIT_TEXT[NO_VULKAN])
        return NO_VULKAN
    t0 = time.monotonic()
    rc = run(argv_, cwd, env, log)
    own = exit_code_text(rc, log)
    if rc == EXIT_EBOOT and a.build_if_needed:
        # the build was lifted from another EBOOT than the game folder's: the same rebuild as a stale build's
        if ask(own + "\n\nRebuild now (as Verify and rebuild does)? This takes as long as the first build.") and \
                update_build(root):
            rc = run(argv_, cwd, env, log)
    elif own:
        notify(own)
    elif time.monotonic() - t0 < EARLY_EXIT and not window_closed(log):
        notify(early_exit_text(rc, log))
    if sys.stdout is not None:
        print(f"log: {log}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
