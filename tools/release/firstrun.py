#!/usr/bin/env python3
"""The first run: check the player's own files, then patch, lift and build the port on their machine, as one command
with one progress bar, and remember where the files are.

    tools/release/firstrun.py [--game DIR] [--eboot FILE] [--psarc FILE] [--dev-flash DIR]
                              [--dlss accept|decline] [--jobs N] [--root DIR] [--status-file FILE] [--stop-file FILE]
                              [--bar] [--dry-run]
    tools/release/firstrun.py --rpcs3 [DIR] [other options]   (the game from the player's RPCS3)
    tools/release/firstrun.py --interactive [same options: they become the answers it offers]
    tools/release/firstrun.py --remove-desktop
    tools/release/firstrun.py --save-paths [--game DIR] [--eboot FILE] ...   (the Windows setup program)
    tools/install.sh                        (the same as --interactive --bar)

After Project Pyrrhus's and Project Helghast's tools/release/firstrun.py (undecuple, MIT): the table of steps with
weights and done-markers, the PROGRESS / RESULT / REASON lines, the status and stop files, the job count from free
memory and the log with the home folder taken out are theirs. The steps are ours: they run this repository's own
tools (tools/check-game-files.py and tools/kzbuild.py's patch, lift, vfs and build, which tools/ps3recomp-patched.sh,
tools/lift.sh, tools/setup-vfs.sh and tools/build.sh run too), so a build made here is the one docs/BUILDING.md
describes. On Windows the same steps build KZ1HD.exe with the pinned toolchain (tools/release/toolchain.json); there
is no venv there (ps3recomp's lifters need only the standard library, CMake and Ninja come with the toolchain).

The paths: each one from its option, else the environment (the KZ_* variables of tools/env.sh), else the last run's
work/paths.env, else the defaults under Game/. Once the check passes they are
written to work/paths.env, which tools/env.sh reads, so tools/play.sh and the other scripts find them without
exports (a variable set in the environment still wins).

--rpcs3 [DIR] (tools/release/rpcs3_import.py): the installed game folder and its data_ps3.psarc.edat
are taken from the player's RPCS3 (DIR, else the usual places), and that RPCS3 decrypts a copy of EBOOT.BIN into
work/prep (checked by SHA-256), and of the .edat too for a PlayStation Store copy (bound to the buyer's licence, which
the port cannot open); those paths are then saved like any others. --interactive offers it
when an RPCS3 with the game is found and no paths were saved before.

The steps, each skipped when its marker in work/.firstrun says its inputs have not changed:
  check     tools/check-game-files.py (every run; a missing or different required file stops here, and so does a CPU
            below the x86-64 level the build is made for, which ends --interactive without asking again)
  venv      work/venv with CMake, Ninja and ps3recomp's Python packages (pip: the one step that downloads; Linux
            with the system's compilers only: not on Windows nor with KZ_LINUX_TOOLCHAIN=pinned)
  patch     work/ps3recomp-patched: ps3recomp at its pinned commit with port/patches (a release folder ships it)
  lift      work/lift: the game's code as C++ (kzbuild.py lift)
  vfs       work/vfs: the folder the game runs from (kzbuild.py vfs; saves and settings there are kept)
  dlss      only when the player accepted NVIDIA's DLSS SDK licence (--dlss accept, or a yes to --interactive's
            question; the choice is kept in work/dlss-choice): the DLSS SDK files fetched from NVIDIA's repository and
            checked (tools/release/fetch_toolchain.py --set dlss-sdk, work/toolchain-cache/dlss-sdk); the compile
            then builds DLSS in and puts its runtime library beside the program. Declined or never answered: no
            download, no DLSS (the PC menu shows it as "(not built)"). On Windows the runtime library is
            nvngx_dlss.dll beside KZ1HD.exe, on the player's machine only. Run again with --dlss accept or
            --dlss decline to change the choice (the Windows setup passes its checkbox's answer)
  compile   work/build/KZ1HD, on Windows work/build-win/KZ1HD.exe (kzbuild.py build; every run: Ninja itself
            skips what is up to date). When the build stamp names another version or patch set than this tree (an
            install over an earlier release), the build folder in work/ is emptied first and everything is
            compiled again: the release files all carry one fixed date, older than the objects an earlier build
            left, so Ninja would take those objects as up to date (outside work/ only CMakeCache.txt is removed)
  data      only when there is no decrypted data_ps3.psarc: the built game runs once, without a window, until it
            has opened the game folder's USRDIR/data_ps3.psarc.edat into work/edat-cache (kzbuild.py data; it
            checks the result's SHA-256 and takes the movies out of it; every run, done at once after the first)
  stamp     work/build-stamp.json: the version, the patch set and the inputs this build was made from

After a build, --desktop (or a yes to --interactive's question) writes the applications-menu entry
($XDG_DATA_HOME/applications/project-vekta.desktop, icon assets/icon.svg) that starts tools/play.sh from this tree;
--remove-desktop takes it out again (Linux; on Windows the installer makes the shortcuts). --save-paths writes the
paths given (only those; the others keep their saved value) into work/paths.env and stops, without a check: the
setup program keeps the player's answers so, and the build or the first start then finds them.

Output: the steps' own lines, and PROGRESS step=i/n name=.. done=.. total=.. elapsed=s pct=..; at the end
RESULT ok=1|0 step=<the step that stopped it, or -> elapsed=s, with REASON <text> lines before a RESULT ok=0. --bar
prints one console progress bar instead of the raw lines (they still go to the log). The log is
work/logs/firstrun-<time>.log (the last five are kept), with the home folder written as ~. Exit 0 = built, 1 = a step
failed, 2 = bad arguments, 130 = stopped (Ctrl+C or the stop file): the next run resumes at that step.
Standard library only, Python 3.8+.
"""
import argparse
import dataclasses
import datetime
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MARKER_DIR = ".firstrun"
PATHS_FILE = "paths.env"
KEEP_LOGS = 5
GIB = 1 << 30
NINJA_RE = re.compile(r"^\[(\d+)/(\d+)\]")
REASON_MAX = 900
DLSS_CHOICE = "dlss-choice"
DLSS_SET = "dlss-sdk"
DLSS_LICENSE_URL = "https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt"
# The player's files: (option, variable, what it is, default under the root); tools/env.sh has the same defaults.
INPUTS = [
    ("game", "KZ_GAME", "the installed game folder (NPUA80856: PARAM.SFO, TROPDIR, USRDIR)",
     ("Game", "NPUA80856")),
    ("eboot", "KZ_EBOOT", "the decrypted EBOOT (EBOOT.elf)", ("Game", "EBOOT.elf")),
    ("psarc", "KZ_PSARC", "the decrypted data archive (data_ps3.psarc; optional: without it the game folder's own "
     "USRDIR/data_ps3.psarc.edat is read)", ("Game", "data_ps3.psarc")),
]
# What check-game-files.py names each input in its MISSING / WRONG / DIFFERENT lines.
INPUT_OF = {"PARAM.SFO": "game", "TROPHY.TRP": "game", "data_ps3.psarc.edat": "game", "EBOOT.elf": "eboot",
            "data_ps3.psarc": "psarc"}


def _h16(text):
    return hashlib.sha256(text.encode()).hexdigest()[:16]


def tree_hash(root, rels):
    """One hash of the named files and folders under root (relative path and content), in a fixed order; a missing
    one counts as missing, so adding it changes the hash."""
    h = hashlib.sha256()
    for rel in rels:
        p = os.path.join(root, rel)
        files = [p] if os.path.isfile(p) else sorted(
            os.path.join(d, f) for d, _, fs in os.walk(p) for f in fs) if os.path.isdir(p) else []
        if not files:
            h.update(f"missing {rel}\n".encode())
        for f in files:
            h.update(os.path.relpath(f, root).replace(os.sep, "/").encode() + b"\0")
            with open(f, "rb") as fh:
                h.update(hashlib.sha256(fh.read()).digest())
    return h.hexdigest()[:16]


def file_id(path):
    """A file's identity without reading it: its real path, size and mtime ("" when it is not there). The check step
    hashes the files; this only notices that the player pointed at another one, or replaced it."""
    try:
        st = os.stat(path)
        return f"{os.path.realpath(path)}:{st.st_size}:{int(st.st_mtime)}"
    except OSError:
        return ""


# ---------------------------------------------------------------------------------------------------------- context
@dataclasses.dataclass
class Ctx:
    root: str
    paths: dict                                  # option name -> path (INPUTS)
    jobs: int = 1
    low_priority: bool = False                   # little free memory: the steps run at a lower priority
    work: str = ""
    python: str = sys.executable
    dlss: str = ""                               # "accept", "decline" or "" (never answered: no DLSS)
    cache: dict = dataclasses.field(default_factory=dict, repr=False)

    def __post_init__(self):
        self.work = self.work or os.environ.get("KZ_WORK") or os.path.join(self.root, "work")

    def dlss_on(self):
        """DLSS is built in only when the player accepted its licence, and never with the pinned Linux toolchain
        (dlss_unavailable)."""
        return self.dlss == "accept" and not dlss_unavailable()

    def w(self, *rel):
        return os.path.join(self.work, *rel)

    def t(self, *rel):
        return os.path.join(self.root, "tools", *rel)

    def env(self):
        e = dict(os.environ)
        for name, var, _, _ in INPUTS:
            if self.paths.get(name):
                e[var] = self.paths[name]
        e["KZ_WORK"] = self.work
        if self.dlss_on():
            e["KZ_DLSS_SDK"] = self.w("toolchain-cache", DLSS_SET)
        else:
            e.pop("KZ_DLSS_SDK", None)
        e["KZ_JOBS"] = str(self.jobs)
        e["PYTHONUNBUFFERED"] = "1"
        return e

    def part(self, key):
        """The inputs a step's marker depends on, as one hash per kind, computed once a run."""
        if key not in self.cache:
            r = self.root
            if key == "files":
                ids = {n: file_id(self.paths.get(n, "")) for n, *_ in INPUTS}
                ids["edat"] = file_id(os.path.join(self.paths.get("game", ""), "USRDIR", "data_ps3.psarc.edat"))
                v = _h16(json.dumps(ids, sort_keys=True))
            elif key == "venv":
                v = tree_hash(r, ["third_party/ps3recomp/requirements.txt"]) + sys.version.split()[0]
            elif key == "patch":
                v = tree_hash(r, ["port/patches", "third_party/ps3recomp.pin", "tools/kzbuild.py"]) + \
                    self.pin()
            elif key == "lift":
                v = tree_hash(r, ["tools/kzbuild.py", "tools/extract_spu_modules.py", "re/function_seeds.txt",
                                  "port/kz1hd/native", "port/kz1hd/native_ppu"])
            elif key == "vfs":
                v = tree_hash(r, ["tools/kzbuild.py", "tools/psarc_ls.py"])
            elif key == "dlss":
                v = _h16(str(self.dlss_on())) + tree_hash(r, ["tools/release/toolchain.json"])
            else:
                raise KeyError(key)
            self.cache[key] = v
        return self.cache[key]

    def pin(self):
        """ps3recomp's commit: the source archive's third_party/ps3recomp.pin, else the submodule's HEAD."""
        try:
            with open(os.path.join(self.root, "third_party", "ps3recomp.pin")) as f:
                return f.read().strip()
        except OSError:
            pass
        try:
            return subprocess.run(["git", "-C", os.path.join(self.root, "third_party", "ps3recomp"), "rev-parse",
                                   "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return "unknown"

    def fingerprint(self, keys):
        return _h16("|".join(self.part(k) for k in keys))


@dataclasses.dataclass
class Step:
    name: str                                   # short id, also the marker file name
    label: str                                  # what the bar shows
    weight: float                               # share of the whole bar
    commands: object                            # Ctx -> list of argv lists, run in order
    keys: tuple = ()                            # Ctx.part kinds its marker depends on
    always: bool = False                        # run even when its marker says done
    ninja: bool = False                         # "[x/y]" lines are its progress
    after: object = None                        # Ctx -> None, in-process work after the commands
    before: object = None                       # Ctx -> str or None, in-process work before them (what it did)


def kzbuild(*args):
    return lambda c: [[c.python, c.t("kzbuild.py"), *args]]


def prepatched(c):
    """A release folder: no ps3recomp submodule, the patched tree shipped (tools/release/assemble_payload.py)."""
    return not os.path.isfile(os.path.join(c.root, "third_party", "ps3recomp", "CMakeLists.txt")) and \
        os.path.isfile(c.w("ps3recomp-patched", ".patchset"))


def cmd_patch(c):
    return [] if prepatched(c) else kzbuild("patch")(c)


def play_command(root):
    """What starts the game from this tree: the release folder's launcher (Project Vekta.exe, project-vekta), else
    tools/release/launch.py on Windows and tools/play.sh elsewhere."""
    if os.name == "nt":
        exe = os.path.join(root, "Project Vekta.exe")
        return exe if os.path.isfile(exe) else f"python {os.path.join(root, 'tools', 'release', 'launch.py')}"
    entry = os.path.join(root, "project-vekta")
    return entry if os.path.isfile(entry) else os.path.join(root, "tools", "play.sh")


def cmd_venv(c):
    # Windows and the pinned Linux toolchain bring CMake and Ninja; the lifters need only the standard library
    if os.name == "nt" or os.environ.get("KZ_LINUX_TOOLCHAIN") == "pinned":
        return []
    venv = c.w("venv")
    cmds = [] if os.path.isfile(os.path.join(venv, "bin", "python")) else [[c.python, "-m", "venv", venv]]
    return cmds + [[os.path.join(venv, "bin", "pip"), "install", "--disable-pip-version-check", "-q", "cmake",
                    "ninja", "-r", os.path.join(c.root, "third_party", "ps3recomp", "requirements.txt")]]


DLSS_LINUX_FOLDER = ("NVIDIA DLSS is not available in the Linux release folder (NVIDIA's Linux library is built "
                     "for GCC's C++ library, the folder's pinned toolchain uses LLVM's): this build uses AMD FSR 3.1. "
                     "For DLSS on Linux, build with your system's compilers (tools/install.sh, docs/BUILDING.md).")


def dlss_unavailable(environ=None):
    """True for the Linux release folder's pinned toolchain (KZ_LINUX_TOOLCHAIN=pinned): Zig links libc++, and
    libnvsdk_ngx.a needs libstdc++, so KZ1HD would not link with DLSS. Windows and the system's compilers are fine."""
    environ = os.environ if environ is None else environ
    return os.name != "nt" and environ.get("KZ_LINUX_TOOLCHAIN") == "pinned"


def cmd_dlss(c):
    if not c.dlss_on():
        return []
    return [[c.python, c.t("release", "fetch_toolchain.py"), "--set", DLSS_SET, "--accept-license", "--cache",
             c.w("toolchain-cache")]]


def read_dlss(work):
    try:
        with open(os.path.join(work, DLSS_CHOICE), encoding="utf-8") as f:
            v = f.read().strip()
        return v if v in ("accept", "decline") else ""
    except OSError:
        return ""


def write_dlss(work, choice):
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, DLSS_CHOICE), "w", encoding="utf-8", newline="\n") as f:
        f.write(choice + "\n")


def read_patchset(work):
    try:
        with open(os.path.join(work, "ps3recomp-patched", ".patchset")) as f:
            return f.read().strip()
    except OSError:
        return None


def read_stamp(work):
    try:
        with open(os.path.join(work, "build-stamp.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def build_dir(c):
    """The compile step's CMake folder, as kzbuild.py picks it (KZ_BUILD wins)."""
    return os.environ.get("KZ_BUILD") or c.w("build-win" if os.name == "nt" else "build")


def moved_from(c):
    """The port folder the build folder's CMake cache was made in, when that is not this one (the folder was moved or
    renamed since: CMake refuses such a cache, and Ninja would make the old folders again); else "". After Project
    Helghast's root_moved (undecuple, MIT); written here."""
    try:
        with open(os.path.join(build_dir(c), "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
            home = next((ln.split("=", 1)[1].strip() for ln in f if ln.startswith("CMAKE_HOME_DIRECTORY:")), "")
    except OSError:
        return ""
    here = os.path.join(c.root, "port", "kz1hd")
    if not home or os.path.normcase(os.path.abspath(home)) == os.path.normcase(os.path.abspath(here)):
        return ""
    tail = os.path.normcase(os.path.normpath(home)).replace("\\", "/")
    return os.path.dirname(os.path.dirname(os.path.normpath(home))) if tail.endswith("/port/kz1hd") else home


def heal_moved_paths(c):
    """After a move, the saved paths that pointed inside the old port folder (the defaults under Game/, the RPCS3
    route's work/prep) and are gone are pointed at the same place in this one, when it is there. Returns the old
    folder, or ""."""
    old = moved_from(c)
    if not old or old.endswith(os.path.join("port", "kz1hd")):
        return ""
    saved, changed = read_paths(c.work), False
    for name, p in saved.items():
        rel = os.path.relpath(p, old) if os.path.isabs(p) else ""
        if rel and not rel.startswith("..") and not os.path.exists(p) and os.path.exists(os.path.join(c.root, rel)):
            saved[name] = c.paths[name] = os.path.join(c.root, rel)
            changed = True
    if changed:
        write_paths(c.work, saved)
    return old


def before_compile(c):
    """A CMake folder left by another version or patch set (an install over an earlier release, the stamp of the
    build it holds says which) is emptied, so nothing that version compiled, configured or cached carries over. A
    folder without a stamp is treated the same. Removing only the cache is not enough: the setup program and the
    release archives give every source file the same fixed date (2026-01-01, for the same bytes from the same
    commit), older than any object a build made, so Ninja kept the earlier version's objects and the link failed
    on functions the new sources call (0.3.1 to 0.4.0, issue #18). A build folder outside work/ (KZ_BUILD set by
    hand) only loses its cache."""
    bdir = build_dir(c)
    cache = os.path.join(bdir, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return None
    inside = os.path.normcase(os.path.abspath(bdir)).startswith(os.path.normcase(os.path.abspath(c.work)) + os.sep)
    old_root = moved_from(c)
    if old_root:
        if inside:
            shutil.rmtree(bdir)
        else:
            os.remove(cache)
        return f"a fresh build: the port folder was moved or renamed (it was {old_root})"
    stamp = read_stamp(c.work) or {}
    want = {"version": read_version(c.root), "patchset": read_patchset(c.work)}
    old = {k: stamp.get(k) for k in want}
    if old == want:
        return None
    if inside:
        shutil.rmtree(bdir)
        return f"a clean build: the build folder was made from {old}, this tree is {want}"
    os.remove(cache)
    return f"a fresh CMake configuration: the build folder was made from {old}, this tree is {want}"


def after_stamp(c):
    stamp = {"version": read_version(c.root), "built": datetime.datetime.now().isoformat(timespec="seconds"),
             "inputs": {k: c.part(k) for k in ("files", "patch", "lift", "vfs")}, "dlss": c.dlss_on()}
    patchset = read_patchset(c.work)
    if patchset is not None:
        stamp["patchset"] = patchset
    with open(c.w("build-stamp.json"), "w", encoding="utf-8") as f:
        json.dump(stamp, f, indent=2)
        f.write("\n")


# Weights from the maintainer's run of a fresh v0.1.0 folder (2026-10-31, 16 threads): check 3 s, patch ~10 s,
# lift 37 s, vfs 30 s, compile 109 s; the venv's pip about 15 s.
STEPS = [
    Step("check", "Checking your game files", 2,
         lambda c: [[c.python, c.t("check-game-files.py"), "--root", c.root, "--work", c.work]], always=True),
    Step("venv", "Installing the build tools", 6, cmd_venv, keys=("venv",)),
    Step("patch", "Patching ps3recomp", 5, cmd_patch, keys=("patch",)),
    Step("lift", "Translating the game's code", 20, kzbuild("lift"), keys=("files", "patch", "lift")),
    Step("vfs", "Preparing the game folder", 15, kzbuild("vfs"), keys=("files", "vfs")),
    Step("dlss", "Downloading NVIDIA DLSS", 3, cmd_dlss, keys=("dlss",), always=True),
    Step("compile", "Compiling", 51, kzbuild("build"), always=True, ninja=True, before=before_compile),
    # Only with the game's own data_ps3.psarc.edat: the built game opens it once (36 s on an NVMe drive; it returns
    # at once when that was done before, or with a decrypted data_ps3.psarc).
    Step("data", "Preparing the game's data", 8, kzbuild("data"), always=True),
    Step("stamp", "Finishing", 1, lambda c: [], always=True, after=after_stamp),
]


def read_version(root):
    try:
        with open(os.path.join(root, "VERSION")) as f:
            return f.read().strip()
    except OSError:
        return "unknown"


# ----------------------------------------------------------------------------------------------------- helpers
def available_ram():
    """Available memory in bytes (/proc/meminfo; GlobalMemoryStatusEx on Windows), or None."""
    if os.name == "nt":
        try:
            import ctypes

            class MemStatus(ctypes.Structure):
                _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong)] + \
                           [(n, ctypes.c_ulonglong) for n in ("total", "avail", "tpage", "apage", "tvirt", "avirt",
                                                              "aext")]
            st = MemStatus()
            st.dwLength = ctypes.sizeof(MemStatus)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st)):
                return int(st.avail)
        except (OSError, AttributeError, ValueError):
            pass
        return None
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) * 1024
    except (OSError, ValueError):
        pass
    return None


def jobs_for(avail, cores=None):
    """Compile and lift jobs from free memory: Pyrrhus's clang rule, (available - 3 GiB) / 1 GiB, 1..min(cores, 16).
    The maintainer's build peaks well under 1 GiB per clang job; the 3 GiB cover the lift's largest process and the
    link."""
    cores = cores or os.cpu_count() or 1
    if not avail:
        return max(1, min(cores, 4))
    return max(1, min(cores, 16, int((avail - 3 * GIB) / GIB)))


LOW_MEMORY = 8 * GIB


def low_memory(avail):
    """Little free memory (under 8 GiB when the build starts): the build's processes then run at a lower priority,
    so the rest of the PC stays usable while every core compiles. After Project Helghast's low_memory_for and
    lower_priority (undecuple, MIT); written here."""
    return bool(avail) and avail < LOW_MEMORY


def priority_kw(low):
    """subprocess.Popen arguments for a lower priority (POSIX nice 10, Windows below normal), else none."""
    if not low:
        return {}
    if os.name == "nt":
        return {"creationflags": 0x00004000}   # BELOW_NORMAL_PRIORITY_CLASS
    return {"preexec_fn": lambda: os.nice(10)}


# Left in work/ after a build and never read again by a later one: the toolchain's downloaded archives (each unpacked
# tree is kept, and fetch_toolchain.py only downloads again when the tree is gone) and the Windows libraries' build
# folder (build_win_deps.py keeps what it installs in work/win-deps).
CLEANUP = (("toolchain-cache", "downloads"), ("win-deps-build",))


def cleanup(work):
    """--cleanup: removes CLEANUP's folders under work; returns the bytes freed."""
    freed = 0
    for rel in CLEANUP:
        p = os.path.join(work, *rel)
        for d, _, files in os.walk(p):
            for f in files:
                try:
                    freed += os.path.getsize(os.path.join(d, f))
                except OSError:
                    pass
        shutil.rmtree(p, ignore_errors=True)
    return freed


def redactor(home=None, windows=None):
    """The home folder becomes ~ in the log (a log is meant to be attached to an issue). Either slash counts as a
    separator (CMake, Ninja and the game write C:/Users/<name> on Windows too), and on Windows the drive also matches
    CMake's mangled form (C_/Users/<name>) and the case is ignored."""
    windows = os.name == "nt" if windows is None else windows
    home = (home or os.path.expanduser("~")).rstrip("/\\")
    if len(home) <= 1:
        return lambda s: s
    parts = [re.escape(p) for p in re.split(r"[/\\]+", home)]
    if windows and re.fullmatch(r"[A-Za-z]:", parts[0].replace("\\", "")):
        parts[0] = parts[0][0] + "[:_]"
    rx = re.compile(r"[/\\]+".join(parts) + r"(?=[/\\]|\b|$)", re.I if windows else 0)
    return lambda s: rx.sub("~", s)


class Log:
    def __init__(self, path, redact):
        self.path, self.redact = path, redact
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.f = open(path, "w", encoding="utf-8", newline="\n")
        self.write(f"# started {datetime.datetime.now().isoformat(timespec='seconds')}")

    def write(self, line):
        self.f.write(self.redact(line.rstrip("\r\n")) + "\n")
        self.f.flush()

    def close(self):
        self.f.close()


def new_log(c, redact):
    d = c.w("logs")
    os.makedirs(d, exist_ok=True)
    old = sorted(n for n in os.listdir(d) if n.startswith("firstrun-") and n.endswith(".log"))
    for n in old[:max(0, len(old) - (KEEP_LOGS - 1))]:
        os.remove(os.path.join(d, n))
    return Log(os.path.join(d, f"firstrun-{datetime.datetime.now():%Y%m%d-%H%M%S}.log"), redact)


def marker_path(c, i, step):
    return c.w(MARKER_DIR, f"{i + 1:02d}-{step.name}.done")


def is_done(c, i, step):
    try:
        with open(marker_path(c, i, step), encoding="utf-8") as f:
            return json.load(f).get("inputs") == c.fingerprint(step.keys)
    except (OSError, ValueError):
        return False


def mark_done(c, i, step, secs):
    p = marker_path(c, i, step)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as f:
        json.dump({"inputs": c.fingerprint(step.keys), "seconds": round(secs, 1)}, f)


# -------------------------------------------------------------------------------------------- the saved paths
def read_paths(work):
    """The last run's work/paths.env as {option: path}; {} when there is none."""
    out = {}
    try:
        with open(os.path.join(work, PATHS_FILE), encoding="utf-8") as f:
            for line in f:
                m = re.match(r'^: "\$\{(\w+):=(.*)\}"$', line.strip())
                if m:
                    name = next((n for n, var, *_ in INPUTS if var == m.group(1)), None)
                    if name:
                        out[name] = m.group(2).replace('\\"', '"').replace("\\$", "$").replace("\\`", "`") \
                                              .replace("\\\\", "\\")
    except OSError:
        pass
    return out


def write_paths(work, paths):
    """work/paths.env for tools/env.sh: `: "${KZ_GAME:=...}"` keeps a value already set in the environment."""
    esc = lambda p: re.sub(r'([\\"$`])', r"\\\1", p)  # noqa: E731
    lines = ["# Written by tools/release/firstrun.py: where your files are. tools/env.sh reads it; a variable set",
             "# in the environment wins. Run tools/install.sh again to change them, or edit this file."]
    lines += [f': "${{{var}:={esc(paths[n])}}}"' for n, var, *_ in INPUTS if paths.get(n)]
    os.makedirs(work, exist_ok=True)
    tmp = os.path.join(work, PATHS_FILE + ".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, os.path.join(work, PATHS_FILE))


def resolve_paths(root, work, given):
    """{option: path}: the option, else the environment, else work/paths.env, else the default."""
    saved = read_paths(work)
    out = {}
    for name, var, _, default in INPUTS:
        p = given.get(name) or os.environ.get(var) or saved.get(name) or os.path.join(root, *default)
        out[name] = os.path.abspath(os.path.expanduser(p))
    return out


# ------------------------------------------------------------------------------------------------------ runner
class Cancelled(Exception):
    pass


class Bar:
    """--bar: one console line that redraws, the step's label and the whole build's percentage."""
    def __init__(self, out):
        self.out, self.last = out, ""

    def show(self, pct, label):
        n = int(pct / 100 * 30)
        line = f"\r[{'#' * n}{'.' * (30 - n)}] {pct:5.1f}%  {label[:40]:<40}"
        if line != self.last:
            self.out.write(line)
            self.out.flush()
            self.last = line

    def end(self):
        if self.last:
            self.out.write("\n")
            self.out.flush()


class Runner:
    def __init__(self, ctx, steps, log, out=sys.stdout, status_file=None, bar=False, quiet=False):
        self.ctx, self.steps, self.log, self.out = ctx, steps, log, out
        self.status_file, self.status_head = status_file, ""
        self.bar = Bar(out) if bar else None
        self.quiet = quiet   # the steps' own lines only: no PROGRESS / RESULT lines on the console
        self.reasons, self.fail_inputs = [], set()
        self.cpu_failed = False   # the check's CPU line failed: no file the player gives fixes it
        self.t0 = time.monotonic()
        tot = sum(s.weight for s in steps) or 1.0
        self.frac = [s.weight / tot for s in steps]
        self.proc, self.stop_requested = None, False

    def watch_stop(self, path, poll=0.25):
        def loop():
            while not os.path.exists(path):
                time.sleep(poll)
            self.stop_requested = True
            self.stop_child()
        threading.Thread(target=loop, name="stop-file", daemon=True).start()

    def emit(self, line, to_log=True, show=True):
        if show and not self.bar and not (self.quiet and line.startswith(("PROGRESS ", "RESULT ", "LOG "))):
            print(line, file=self.out, flush=True)
        if to_log and self.log:
            self.log.write(line)
        if self.status_file and line.startswith(("LOG ", "PROGRESS ", "RESULT ")):
            self.status(line)

    def status(self, line):
        if line.startswith("LOG "):
            self.status_head = line
        tmp = self.status_file + ".tmp"
        try:
            tail = "".join(f"REASON {r}\n" for r in self.reasons) if line.startswith("RESULT ok=0") else ""
            with open(tmp, "w", encoding="utf-8") as f:
                f.write(self.status_head + "\n" + (line if line != self.status_head else "") + "\n" + tail)
            os.replace(tmp, self.status_file)
        except OSError:
            pass

    def progress(self, i, done, total):
        part = (done / total) if total else 0.0
        pct = 100.0 * (sum(self.frac[:i]) + self.frac[i] * min(1.0, part))
        self.emit(f"PROGRESS step={i + 1}/{len(self.steps)} name={self.steps[i].name} done={done} total={total} "
                  f"elapsed={int(time.monotonic() - self.t0)} pct={pct:.1f}")
        if self.bar:
            self.bar.show(pct, self.steps[i].label)

    def run_cmd(self, i, argv):
        step = self.steps[i]
        self.log.write(f"$ {shlex.join(argv) if hasattr(shlex, 'join') else ' '.join(argv)}")
        t = time.monotonic()
        captured = []
        try:
            self.proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                         encoding="utf-8", errors="replace", bufsize=1, env=self.ctx.env(),
                                         cwd=self.ctx.root, **priority_kw(self.ctx.low_priority))
        except OSError as e:
            self.emit(f"cannot start {argv[0]}: {e}")
            return 127, [str(e)]
        try:
            for line in self.proc.stdout:
                line = line.rstrip("\r\n")
                captured.append(line)
                self.emit(line)
                m = NINJA_RE.match(line) if step.ninja else None
                if m:
                    self.progress(i, int(m.group(1)), int(m.group(2)))
            rc = self.proc.wait()
            if self.stop_requested:
                raise Cancelled()
        except KeyboardInterrupt:
            self.stop_child()
            raise Cancelled()
        finally:
            self.proc = None
        self.log.write(f"# exit {rc} after {time.monotonic() - t:.1f} s")
        return rc, captured

    def stop_child(self):
        p = self.proc
        if p and p.poll() is None:
            p.terminate()
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()

    def run(self):
        c, n = self.ctx, len(self.steps)
        try:
            for i, step in enumerate(self.steps):
                if self.stop_requested:
                    raise Cancelled()
                if not step.always and is_done(c, i, step):
                    self.log.write(f"## step {i + 1}/{n} {step.name}: done earlier, skipped")
                    self.progress(i, 1, 1)
                    continue
                self.log.write(f"## step {i + 1}/{n} {step.name}: {step.label}")
                t = time.monotonic()
                did = step.before(c) if step.before else None
                if did:
                    self.emit(did)
                cmds = step.commands(c)
                self.progress(i, 0, len(cmds) or 1)
                for k, argv in enumerate(cmds):
                    rc, out = self.run_cmd(i, argv)
                    if rc != 0:
                        self.emit(f"step {step.name} failed: exit {rc}")
                        return self.failed(step.name, *self.why_failed(step, out, rc))
                    if not step.ninja:
                        self.progress(i, k + 1, len(cmds))
                if step.after:
                    step.after(c)
                if step.name == "check":
                    write_paths(c.work, c.paths)
                self.progress(i, 1, 1)
                self.log.write(f"## step {step.name} done in {time.monotonic() - t:.1f} s")
                mark_done(c, i, step, time.monotonic() - t)
            return self.result(True, "-")
        except (Cancelled, KeyboardInterrupt):
            self.stop_child()
            self.log.write("## stopped: the running step left no marker, the next run resumes there")
            self.result(False, "cancelled")
            return 130

    def why_failed(self, step, out, rc):
        """The check: one reason per file it found missing or different (and which inputs those are); another step:
        its last error line, else its last line."""
        if step.name == "check":
            fails = [ln for ln in out if re.match(r"^(MISSING|WRONG|DIFFERENT|FAIL)\s", ln)]
            for ln in fails:
                label = ln.split()[1].rstrip(":")   # "WRONG    EBOOT.elf: <path>: ..." names it with a colon
                if label in INPUT_OF:
                    self.fail_inputs.add(INPUT_OF[label])
                self.cpu_failed |= label == "CPU"
            if fails:
                return [" ".join(ln.split()) for ln in fails]
        lines = [ln.strip() for ln in out if ln.strip()]
        last = next((ln for ln in reversed(lines) if re.search(r"\b(error|no |FAILED)\b", ln, re.I)),
                    lines[-1] if lines else "")
        return [f'the step "{step.label}" failed (exit {rc})' + (f": {last[:REASON_MAX]}" if last else "")]

    def failed(self, step, *reasons):
        self.reasons = [" ".join(str(r).split())[:REASON_MAX] for r in reasons if str(r).strip()]
        for r in self.reasons:
            self.log.write(r)
            self.emit(f"REASON {r}", to_log=False)
        return self.result(False, step)

    def result(self, ok, step):
        if self.bar:
            self.bar.end()
        self.emit(f"RESULT ok={int(ok)} step={step} elapsed={int(time.monotonic() - self.t0)}")
        return 0 if ok else 1


def dry_run(c, steps, out=sys.stdout):
    print(f"firstrun (dry run): {c.root}, work {c.work}, {c.jobs} jobs, DLSS {'on' if c.dlss_on() else 'off'}", file=out)
    for name, var, what, _ in INPUTS:
        print(f"  {var:<14} {c.paths.get(name, '')}" + ("" if os.path.exists(c.paths.get(name, "")) else
                                                         "   (not there)"), file=out)
    for i, s in enumerate(steps):
        state = "every run" if s.always else ("done" if is_done(c, i, s) else "to do")
        print(f"{i + 1}. {s.name:<8} {state:<9} {s.label}", file=out)
        for argv in s.commands(c):
            print(f"     $ {' '.join(argv)}", file=out)
    return 0


# ------------------------------------------------------------------------------------------------- interactive
# ------------------------------------------------------------------------------------------- the menu entry
DESKTOP_NAME = "project-vekta.desktop"


def desktop_path():
    """The applications-menu entry: $XDG_DATA_HOME/applications (default ~/.local/share/applications)."""
    data = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(data, "applications", DESKTOP_NAME)


def desktop_entry(root):
    """A freedesktop.org entry that starts the game from this tree (play_command: project-vekta in a release folder,
    else tools/play.sh), with assets/icon.svg as its icon."""
    q = lambda p: '"' + re.sub(r'([\\"`$])', r"\\\\\1", p) + '"'  # noqa: E731 (Exec quoting, then the escape layer)
    return "\n".join([
        "[Desktop Entry]",
        "Type=Application",
        "Version=1.5",
        "Name=Project Vekta",
        "GenericName=Killzone HD (PC port)",
        "Comment=Plays the PC port of Killzone HD built from your own game",
        f"Exec={q(play_command(root))}",
        f"Path={root}",
        f"Icon={os.path.join(root, 'assets', 'icon.svg')}",
        "Terminal=false",
        "Categories=Game;ActionGame;",
        "StartupNotify=true",
        "StartupWMClass=project-vekta",   # the game window's class / app id (main.cpp, ps3recomp patch 2081)
        "",
    ])


def install_desktop(root):
    p = desktop_path()
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(desktop_entry(root))
    return p


def remove_desktop():
    p = desktop_path()
    if os.path.exists(p):
        os.remove(p)
        return p
    return None


NOTICE = """Project Vekta {version}: first run
It checks your own files, then translates and builds the game's code on this machine (a few minutes; the Python
packages of the build tools are the only download). Nothing of the game is downloaded, and your files are only read.
What each file must be: docs/GETTING-FILES.md. Enter keeps the path in brackets.
"""


def rpcs3_import():
    """tools/release/rpcs3_import.py (beside this file; the tests replace this function with a stand-in)."""
    import rpcs3_import as m   # noqa: E402
    return m


def use_rpcs3(c, hint=None, say=print, step=None):
    """--rpcs3: the paths from the player's RPCS3 and its decrypted copies (rpcs3_import.prepare); saved in
    work/paths.env. Returns [] when done, else the problems' texts."""
    r = rpcs3_import().prepare(c.work, hint or None, log=say, step=step)
    if r["status"] != "ok":
        return [p["text"] for p in r["problems"]]
    c.paths.update(game=r["game"], eboot=r["eboot"])
    if r.get("psarc"):
        c.paths["psarc"] = r["psarc"]   # a PSN copy: its .edat is bound to the licence, RPCS3 opened it
    elif c.paths.get("psarc") and not os.path.isfile(c.paths["psarc"]):
        c.paths.pop("psarc")   # a wrong saved path (a folder): the port reads the game's own .edat instead
    write_paths(c.work, c.paths)
    made = "EBOOT.elf and data_ps3.psarc" if r.get("psarc") else "EBOOT.elf"
    say(f"From RPCS3: the game in {r['game']}; {made} made by "
        f"your RPCS3 in {os.path.dirname(r['eboot'])}")
    return []


INSTALL_DOC = "https://github.com/gustavomorinaga/project-vekta/blob/main/docs/INSTALL.md"
SHOW_ME_HOW = """How to give the port your game (every step: docs/INSTALL.md, also at """ + INSTALL_DOC + """):
  The easy way, through RPCS3:
    1. In RPCS3, install Killzone HD (NPUA80856) from your own Killzone Trilogy disc, or your PlayStation Store copy
       with its licence.
    2. Choose "Use my RPCS3 install" here: your RPCS3 makes the copies the port needs, in this folder's work/prep.
       Nothing is written into the game's or RPCS3's folders.
  Or, with the decrypted files you already have: choose "I have the files ready" and give the installed game
  folder and EBOOT.elf (docs/GETTING-FILES.md says what each must be)."""


def offer_rpcs3(c, hint, ask=input, say=print):
    """--interactive: "Use my RPCS3 install" (when RPCS3 has the game), "I have the files ready", "Show me how" or
    "My RPCS3 is elsewhere" (the player types its folder, which is looked at instead of the usual places). True when
    the paths came from RPCS3; False leaves them to the questions."""
    def look(h):
        r = rpcs3_import().resolve(h or None)
        if r["status"] == "ok":
            say(f"Found RPCS3 in {r['root']}, with Killzone HD in {r['game']}.")
        else:
            say("Your RPCS3 cannot be used:\n" + "\n".join("  " + p["text"] for p in r["problems"])
                if r["problems"] else "No RPCS3 with Killzone HD was found.")
        return r["status"] == "ok"
    found = look(hint)
    while True:
        say("  1. Use my RPCS3 install (it decrypts a copy of the game's EBOOT, and of a PlayStation Store copy's "
            "data archive, for the port)"
            + ("" if found else ": not available")
            + "\n  2. I have the files ready\n  3. Show me how"
            + "\n  4. My RPCS3 is somewhere else: type its folder")
        got = ask(f"Your choice [{1 if found else 2}]: ").strip().lower()
        if got in ("3", "?", "h", "how"):
            say(SHOW_ME_HOW)
            continue
        if got == "4":
            d = ask(RPCS3_FOLDER_QUESTION).strip().strip("'\"")
            if d:
                hint = os.path.abspath(os.path.expanduser(d))
                found = look(hint)
            continue
        if got in ("2", "n", "no") or (not got and not found):
            return False
        if not found:
            say("RPCS3 cannot be used (above). Fix that and run this again, choose 4 to give its folder, or choose 2.")
            continue
        probs = use_rpcs3(c, hint, say)
        if not probs:
            return True
        say("That did not work:\n" + "\n".join("  " + p for p in probs) + "\nChoose 2 to pick the files yourself.")
        found = False


RPCS3_FOLDER_QUESTION = ("RPCS3's folder: where rpcs3.exe is (a portable RPCS3), RPCS3's config folder (on Linux "
                         "~/.config/rpcs3), or the game folder RPCS3 installed (dev_hdd0/game/NPUA80856): ")


def ask_paths(paths, only=None, ask=input, say=print):
    """Ask for each input (or only those named), offering the current path; returns the new {option: path}."""
    out = dict(paths)
    for name, var, what, _ in INPUTS:
        if only and name not in only:
            continue
        cur = out.get(name, "")
        mark = "" if os.path.exists(cur) else " (not there)"
        got = ask(f"{what}\n  [{cur}]{mark}: ").strip()
        if not sys.stdin.isatty():
            say("")   # answers from a pipe are not echoed: end the prompt's line
        if got:
            out[name] = in_folder(name, os.path.abspath(os.path.expanduser(got.strip("'\""))))
    return out


# The file each file input names; a folder given for one is looked into for it (in_folder).
FILE_OF = {"eboot": "EBOOT.elf", "psarc": "data_ps3.psarc"}


def in_folder(name, path):
    """A folder given where a file is asked for: the file of that name in it (or in its USRDIR) when there is one,
    else the folder itself, which the check then names as a folder."""
    want = FILE_OF.get(name)
    if want and os.path.isdir(path):
        for p in (os.path.join(path, want), os.path.join(path, "USRDIR", want)):
            if os.path.isfile(p):
                return p
    return path


def rpcs3_hint(paths):
    """The RPCS3 folder one of the given paths lies in (the player pointed into RPCS3's dev_hdd0 or dev_flash), else
    None."""
    m = rpcs3_import()
    root_of = getattr(m, "rpcs3_root_of", None)
    for name in ("game", "eboot", "psarc"):
        r = root_of(paths.get(name)) if root_of and paths.get(name) else None
        if r:
            return r
    # a game folder that RPCS3's vfs.yml keeps outside the RPCS3 folder: the RPCS3 that has that very folder
    game_of = getattr(m, "_game_of", None)
    for name in ("game", "eboot"):
        p = paths.get(name)
        if game_of and p and game_of(p):
            r = m.resolve(p)
            if r.get("root") and r.get("game"):
                return p
    return None


DLSS_QUESTION = """NVIDIA DLSS (optional, for NVIDIA RTX graphics cards): this build can download the DLSS SDK from
NVIDIA's own repository and build DLSS in. It is NVIDIA's software under NVIDIA's licence, not this project's:
  {url}
  (its text, downloaded: {path})
Without it the port builds with AMD FSR 3.1 as its temporal upscaler, and DLSS shows as "(not built)".
To change this later: {again} --dlss accept (or --dlss decline)."""


def fetch_license(c):
    """The DLSS SDK licence's text, downloaded for the player to read (fetch_toolchain.py --license-only): its path,
    or None when it could not be fetched."""
    try:
        r = subprocess.run([c.python, c.t("release", "fetch_toolchain.py"), "--set", DLSS_SET, "--license-only",
                            "--cache", c.w("toolchain-cache")], capture_output=True, text=True, env=c.env(), timeout=120)
    except (OSError, subprocess.TimeoutExpired):
        return None
    line = next((ln for ln in r.stdout.splitlines() if ln.startswith(DLSS_SET + " ")), "")
    return line.split(" ", 1)[1] if r.returncode == 0 and line else None


def ask_dlss(c, ask=input, say=print):
    """Once: the licence's link and text, then accept or decline; the answer is kept in work/dlss-choice."""
    path = fetch_license(c) or "(could not be downloaded; read it at the link)"
    say(DLSS_QUESTION.format(url=DLSS_LICENSE_URL, path=path, again="scripts\\run.cmd" if os.name == "nt" else "scripts/run.sh"))
    got = ask("Download NVIDIA DLSS and accept the NVIDIA DLSS SDK licence? [y/N] ").strip().lower()
    c.dlss = "accept" if got in ("y", "yes") else "decline"
    write_dlss(c.work, c.dlss)


def interactive(c, a, ask=input, say=print, out=sys.stdout):
    """Ask for the paths, run the check until it passes (or the player quits), then build with the bar."""
    say(NOTICE.format(version=read_version(c.root)))
    only = None
    hint = getattr(a, "rpcs3", None)
    offered = hint is not None or not read_paths(c.work)
    skip_ask = offered and offer_rpcs3(c, hint, ask, say)
    while True:
        if not skip_ask:
            c.paths = ask_paths(c.paths, only, ask, say)
        skip_ask = False
        log = new_log(c, redactor())
        r = Runner(c, STEPS[:1], log, out=out, status_file=a.status_file, quiet=True)
        rc = r.run()
        log.close()
        if rc == 0:
            break
        say("\nThe check did not pass:\n" + "\n".join("  " + x for x in r.reasons))
        if r.cpu_failed:   # the CPU: asking for the files again or offering RPCS3 would not help
            return 1
        # Paths from an older install (no offer above), or paths into RPCS3's own folders (the game as RPCS3
        # installed it holds only the encrypted EBOOT.BIN): offer the RPCS3 route once more, with that RPCS3
        found = rpcs3_hint(c.paths)
        if found or not offered:
            offered = True
            if found:
                say(f"\nThose paths point at the game as your RPCS3 installed it ({found}), which holds the encrypted "
                    "EBOOT.BIN and .edat; your RPCS3 can make the decrypted copies the port needs.")
            if offer_rpcs3(c, found, ask, say):
                skip_ask = True
                only = None
                continue
        if ask("Enter to choose the files again, q to quit: ").strip().lower() == "q":
            return 1
        only = r.fail_inputs or None
    if not c.dlss and not dlss_unavailable():
        ask_dlss(c, ask, say)
    log = new_log(c, redactor())
    say(f"Building (log: {log.path})")
    r = Runner(c, STEPS, log, out=out, status_file=a.status_file, bar=True)
    if a.stop_file:
        r.watch_stop(a.stop_file)
    rc = r.run()
    log.close()
    if rc == 0 and getattr(a, "cleanup", False):
        say(f"cleanup: {cleanup(c.work) / GIB:.2f} GB freed")
    if rc == 0:
        say(f"Built. Play with: {play_command(c.root)}")
        if os.name != "nt" and (a.desktop or (a.desktop is None and ask("Add Project Vekta to the applications menu? [Y/n] ").strip()
                                                  .lower() not in ("n", "no"))):
            say(f"Menu entry: {install_desktop(c.root)}")
    elif rc == 1:
        say("The build stopped:\n" + "\n".join("  " + x for x in r.reasons) + f"\nThe whole log: {log.path}")
    return rc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    for name, var, what, _ in INPUTS:
        ap.add_argument("--" + name.replace("_", "-"), dest=name, metavar="PATH", help=f"{what} ({var})")
    ap.add_argument("--dlss", choices=("accept", "decline"),
                    help="accept: you accept NVIDIA's DLSS SDK licence (" + DLSS_LICENSE_URL + "), and the build "
                         "downloads the SDK from NVIDIA and builds DLSS in; decline: no DLSS. Kept for later runs "
                         "(work/dlss-choice); --interactive asks once when it was never answered")
    ap.add_argument("--rpcs3", nargs="?", const="", metavar="DIR",
                    help="take the game from your RPCS3 (DIR, else the usual places); your RPCS3 "
                         "decrypts a copy of EBOOT.BIN into work/prep")
    ap.add_argument("--jobs", type=int, help="parallel jobs (default: from free memory)")
    ap.add_argument("--cleanup", action="store_true",
                    help="after a good build, remove what no later build reads (the toolchain's downloaded archives, "
                         "the Windows libraries' build folder)")
    ap.add_argument("--root", default=ROOT, help="the source tree (the tests use a copy)")
    ap.add_argument("--status-file", help="keep the LOG line and the latest PROGRESS/RESULT line in this file")
    ap.add_argument("--stop-file", help="stop when this file appears; the next run resumes there")
    ap.add_argument("--bar", action="store_true", help="one console progress bar instead of the steps' lines")
    ap.add_argument("--interactive", action="store_true", help="ask for the files, check them, then build")
    ap.add_argument("--dry-run", action="store_true", help="print the plan; run and write nothing")
    ap.add_argument("--desktop", action=argparse.BooleanOptionalAction if hasattr(argparse, "BooleanOptionalAction")
                    else "store_true", default=None,
                    help="add (--no-desktop: do not add) the applications-menu entry after a build; "
                         "--interactive asks when neither is given")
    ap.add_argument("--remove-desktop", action="store_true", help="remove the applications-menu entry and stop")
    ap.add_argument("--save-paths", action="store_true",
                    help="write the paths given as options (only those) into work/paths.env and stop: the Windows "
                         "setup program keeps the player's answers this way, before or instead of a build")
    a = ap.parse_args(argv)
    for s in (sys.stdout, sys.stderr):   # a tool's message in another code page must not stop the run
        if hasattr(s, "reconfigure"):
            s.reconfigure(errors="replace")
    if a.jobs is not None and a.jobs < 1:
        print("firstrun: --jobs must be 1 or more", file=sys.stderr)
        return 2
    root = os.path.abspath(a.root)
    work = os.environ.get("KZ_WORK") or os.path.join(root, "work")
    avail = available_ram()
    c = Ctx(root=root, paths=resolve_paths(root, work, {n: getattr(a, n) for n, *_ in INPUTS}),
            jobs=a.jobs or jobs_for(avail), work=work, low_priority=low_memory(avail))
    if a.dlss and not a.dry_run:
        write_dlss(work, a.dlss)
    c.dlss = a.dlss or read_dlss(work)
    if c.dlss == "accept" and dlss_unavailable():
        print(DLSS_LINUX_FOLDER)
    if not a.dry_run:
        old = heal_moved_paths(c)
        if old:
            print(f"The port folder was moved (it was {old}): saved paths inside it now point here, and the build "
                  "is made again")
    if a.save_paths:
        given = {n: os.path.abspath(os.path.expanduser(getattr(a, n))) for n, *_ in INPUTS if getattr(a, n)}
        if not given:
            print("firstrun: --save-paths needs at least one path option", file=sys.stderr)
            return 2
        write_paths(work, dict(read_paths(work), **given))
        print(f"paths: {os.path.join(work, PATHS_FILE)}")
        return 0
    if a.remove_desktop:
        p = remove_desktop()
        print(f"removed {p}" if p else f"no menu entry at {desktop_path()}")
        return 0
    if a.dry_run:
        return dry_run(c, STEPS)
    if a.interactive:
        try:
            return interactive(c, a)
        except (KeyboardInterrupt, EOFError):
            print("\nstopped")
            return 130
    log = new_log(c, redactor())
    r = Runner(c, STEPS, log, status_file=a.status_file, bar=a.bar)
    r.emit(f"LOG {log.path}")
    if a.rpcs3 is not None:
        # Before the steps, but inside the status protocol: the setup's build page waits for a RESULT line, so a
        # failure here must end with one (it hung on a failed --rpcs3 that only printed to stderr).
        r.emit(f"PROGRESS step=0/{len(STEPS)} name=rpcs3 done=0 total=0 elapsed=0 pct=0.0")
        t0 = time.monotonic()
        # one PROGRESS line per file RPCS3 decrypts, naming it: the setup shows which one (the PSN archive takes a
        # minute or more, and the page would otherwise look stuck)
        step = lambda i, n, name: r.emit(  # noqa: E731
            f"PROGRESS step=0/{len(STEPS)} name=rpcs3 done={i} total={n} elapsed={int(time.monotonic() - t0)} "
            f"pct=0.0 file={name}")
        try:
            probs = use_rpcs3(c, a.rpcs3, say=r.emit, step=step)
        except Exception as e:   # noqa: BLE001  (any surprise still ends the setup's wait)
            probs = [f"the RPCS3 step stopped: {type(e).__name__}: {e}"]
        if probs:
            print("firstrun: your RPCS3 cannot be used:\n" + "\n".join("  " + p for p in probs), file=sys.stderr)
            rc = r.failed("rpcs3", *("your RPCS3 cannot be used: " + p for p in probs))
            log.close()
            return rc
    if a.stop_file:
        r.watch_stop(a.stop_file)
    rc = r.run()
    log.close()
    if rc == 0 and a.cleanup:
        print(f"cleanup: {cleanup(c.work) / GIB:.2f} GB freed")
    if rc == 0 and a.desktop and os.name != "nt":
        print(f"Menu entry: {install_desktop(c.root)}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
