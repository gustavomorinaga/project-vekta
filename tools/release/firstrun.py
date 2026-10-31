#!/usr/bin/env python3
"""The first run: check the player's own files, then patch, lift and build the port on their machine, as one command
with one progress bar, and remember where the files are.

    tools/release/firstrun.py [--game DIR] [--eboot FILE] [--psarc FILE] [--libsre FILE] [--dev-flash DIR]
                              [--jobs N] [--root DIR] [--status-file FILE] [--stop-file FILE] [--bar] [--dry-run]
    tools/release/firstrun.py --interactive [same options: they become the answers it offers]
    tools/release/firstrun.py --remove-desktop
    tools/install.sh                        (the same as --interactive --bar)

After Project Pyrrhus's and Project Helghast's tools/release/firstrun.py (undecuple, MIT): the table of steps with
weights and done-markers, the PROGRESS / RESULT / REASON lines, the status and stop files, the job count from free
memory and the log with the home folder taken out are theirs. The steps are ours: they run this repository's own
tools (tools/check-game-files.py and tools/kzbuild.py's patch, lift, vfs and build, which tools/ps3recomp-patched.sh,
tools/lift.sh, tools/setup-vfs.sh and tools/build.sh run too), so a build made here is the one docs/BUILDING.md
describes. On Windows the same steps build KZ1HD.exe with the pinned toolchain (tools/release/toolchain.json); there
is no venv there (ps3recomp's lifters need only the standard library, CMake and Ninja come with the toolchain).

The paths: each one from its option, else the environment (the KZ_* variables of tools/env.sh), else the last run's
work/paths.env, else the defaults under Game/ (PS3_DEV_FLASH: RPCS3's dev_flash). Once the check passes they are
written to work/paths.env, which tools/env.sh reads, so tools/play.sh and the other scripts find them without
exports (a variable set in the environment still wins).

The steps, each skipped when its marker in work/.firstrun says its inputs have not changed:
  check     tools/check-game-files.py (every run; a missing or different required file stops here)
  venv      work/venv with CMake, Ninja and ps3recomp's Python packages (pip: the one step that downloads; Linux)
  patch     work/ps3recomp-patched: ps3recomp at its pinned commit with port/patches (a release folder ships it)
  lift      work/lift and work/fw: the game's and the firmware's code as C++ (kzbuild.py lift)
  vfs       work/vfs: the folder the game runs from (kzbuild.py vfs; saves and settings there are kept)
  compile   work/build/KZ1HD, on Windows work/build-win/KZ1HD.exe (kzbuild.py build; every run: Ninja itself
            skips what is up to date)
  stamp     work/build-stamp.json: the version, the patch set and the inputs this build was made from

After a build, --desktop (or a yes to --interactive's question) writes the applications-menu entry
($XDG_DATA_HOME/applications/project-vekta.desktop, icon assets/icon.svg) that starts tools/play.sh from this tree;
--remove-desktop takes it out again (Linux; on Windows the installer makes the shortcuts).

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
# The player's files: (option, variable, what it is, default under the root); tools/env.sh has the same defaults.
INPUTS = [
    ("game", "KZ_GAME", "the installed game folder (NPUA80856: PARAM.SFO, TROPDIR, USRDIR)",
     ("Game", "NPUA80856")),
    ("eboot", "KZ_EBOOT", "the decrypted EBOOT (EBOOT.elf)", ("Game", "EBOOT.elf")),
    ("psarc", "KZ_PSARC", "the decrypted data archive (data_ps3.psarc)", ("Game", "data_ps3.psarc")),
    ("libsre", "KZ_LIBSRE", "the decrypted libsre from PS3 firmware 4.93 (libsre.prx)", ("Game", "libsre.prx")),
    ("dev_flash", "PS3_DEV_FLASH", "RPCS3's dev_flash folder (for the MP3 decoder; optional)", None),
]
# What check-game-files.py names each input in its MISSING / WRONG / DIFFERENT lines.
INPUT_OF = {"PARAM.SFO": "game", "TROPHY.TRP": "game", "EBOOT.elf": "eboot", "data_ps3.psarc": "psarc",
            "libsre.prx": "libsre", "flashMP3.pic": "dev_flash"}


def default_dev_flash():
    return os.path.join(os.path.expanduser("~"), ".config", "rpcs3", "dev_flash")


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
    work: str = ""
    python: str = sys.executable
    cache: dict = dataclasses.field(default_factory=dict, repr=False)

    def __post_init__(self):
        self.work = self.work or os.environ.get("KZ_WORK") or os.path.join(self.root, "work")

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
        e["KZ_JOBS"] = str(self.jobs)
        e["PYTHONUNBUFFERED"] = "1"
        return e

    def part(self, key):
        """The inputs a step's marker depends on, as one hash per kind, computed once a run."""
        if key not in self.cache:
            r = self.root
            if key == "files":
                v = _h16(json.dumps({n: file_id(self.paths.get(n, "")) for n, *_ in INPUTS}, sort_keys=True))
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


def kzbuild(*args):
    return lambda c: [[c.python, c.t("kzbuild.py"), *args]]


def prepatched(c):
    """A release folder: no ps3recomp submodule, the patched tree shipped (tools/release/assemble_payload.py)."""
    return not os.path.isfile(os.path.join(c.root, "third_party", "ps3recomp", "CMakeLists.txt")) and \
        os.path.isfile(c.w("ps3recomp-patched", ".patchset"))


def cmd_patch(c):
    return [] if prepatched(c) else kzbuild("patch")(c)


def play_command(root):
    """What starts the game from this tree: play.cmd in a Windows release folder, else tools/play.sh."""
    if os.name == "nt":
        cmd = os.path.join(root, "play.cmd")
        return cmd if os.path.isfile(cmd) else f"python {os.path.join(root, 'tools', 'release', 'launch.py')}"
    return os.path.join(root, "tools", "play.sh")


def cmd_venv(c):
    if os.name == "nt":
        return []
    venv = c.w("venv")
    cmds = [] if os.path.isfile(os.path.join(venv, "bin", "python")) else [[c.python, "-m", "venv", venv]]
    return cmds + [[os.path.join(venv, "bin", "pip"), "install", "--disable-pip-version-check", "-q", "cmake",
                    "ninja", "-r", os.path.join(c.root, "third_party", "ps3recomp", "requirements.txt")]]


def after_stamp(c):
    stamp = {"version": read_version(c.root), "built": datetime.datetime.now().isoformat(timespec="seconds"),
             "inputs": {k: c.part(k) for k in ("files", "patch", "lift", "vfs")}}
    try:
        with open(c.w("ps3recomp-patched", ".patchset")) as f:
            stamp["patchset"] = f.read().strip()
    except OSError:
        pass
    with open(c.w("build-stamp.json"), "w", encoding="utf-8") as f:
        json.dump(stamp, f, indent=2)
        f.write("\n")


# Weights from the maintainer's run of a fresh v0.1.0 folder (2026-10-31, 16 threads): check 3 s, patch ~10 s,
# lift 37 s, vfs 30 s, compile 109 s; the venv's pip about 15 s.
STEPS = [
    Step("check", "Checking your game files", 2, lambda c: [[c.python, c.t("check-game-files.py")]], always=True),
    Step("venv", "Installing the build tools", 6, cmd_venv, keys=("venv",)),
    Step("patch", "Patching ps3recomp", 5, cmd_patch, keys=("patch",)),
    Step("lift", "Translating the game's code", 20, kzbuild("lift"), keys=("files", "patch", "lift")),
    Step("vfs", "Preparing the game folder", 15, kzbuild("vfs"), keys=("files", "vfs")),
    Step("compile", "Compiling", 51, kzbuild("build"), always=True, ninja=True),
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


def redactor(home=None):
    """The home folder becomes ~ in the log (a log is meant to be attached to an issue)."""
    home = (home or os.path.expanduser("~")).rstrip("/")
    if len(home) <= 1:
        return lambda s: s
    rx = re.compile(re.escape(home) + r"(?=[/\\]|\b|$)", re.I if os.name == "nt" else 0)
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
        p = given.get(name) or os.environ.get(var) or saved.get(name)
        if not p:
            p = os.path.join(root, *default) if default else default_dev_flash()
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
                                         cwd=self.ctx.root)
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
            fails = [ln for ln in out if re.match(r"^(MISSING|WRONG|DIFFERENT)\s", ln)]
            for ln in fails:
                label = ln.split()[1]
                if label in INPUT_OF:
                    self.fail_inputs.add(INPUT_OF[label])
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
    print(f"firstrun (dry run): {c.root}, work {c.work}, {c.jobs} jobs", file=out)
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
    """A freedesktop.org entry that starts tools/play.sh from this tree, with assets/icon.svg as its icon."""
    q = lambda p: '"' + re.sub(r'([\\"`$])', r"\\\\\1", p) + '"'  # noqa: E731 (Exec quoting, then the escape layer)
    return "\n".join([
        "[Desktop Entry]",
        "Type=Application",
        "Version=1.5",
        "Name=Project Vekta",
        "GenericName=Killzone HD (PC port)",
        "Comment=Plays the PC port of Killzone HD built from your own game",
        f"Exec={q(os.path.join(root, 'tools', 'play.sh'))}",
        f"Path={root}",
        f"Icon={os.path.join(root, 'assets', 'icon.svg')}",
        "Terminal=false",
        "Categories=Game;ActionGame;",
        "StartupNotify=true",
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
            out[name] = os.path.abspath(os.path.expanduser(got.strip("'\"")))
    return out


def interactive(c, a, ask=input, say=print, out=sys.stdout):
    """Ask for the paths, run the check until it passes (or the player quits), then build with the bar."""
    say(NOTICE.format(version=read_version(c.root)))
    only = None
    while True:
        c.paths = ask_paths(c.paths, only, ask, say)
        log = new_log(c, redactor())
        r = Runner(c, STEPS[:1], log, out=out, status_file=a.status_file, quiet=True)
        rc = r.run()
        log.close()
        if rc == 0:
            break
        say("\nThe check did not pass:\n" + "\n".join("  " + x for x in r.reasons))
        if ask("Enter to choose the files again, q to quit: ").strip().lower() == "q":
            return 1
        only = r.fail_inputs or None
    log = new_log(c, redactor())
    say(f"Building (log: {log.path})")
    r = Runner(c, STEPS, log, out=out, status_file=a.status_file, bar=True)
    if a.stop_file:
        r.watch_stop(a.stop_file)
    rc = r.run()
    log.close()
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
    ap.add_argument("--jobs", type=int, help="parallel jobs (default: from free memory)")
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
    a = ap.parse_args(argv)
    if a.jobs is not None and a.jobs < 1:
        print("firstrun: --jobs must be 1 or more", file=sys.stderr)
        return 2
    root = os.path.abspath(a.root)
    work = os.environ.get("KZ_WORK") or os.path.join(root, "work")
    c = Ctx(root=root, paths=resolve_paths(root, work, {n: getattr(a, n) for n, *_ in INPUTS}),
            jobs=a.jobs or jobs_for(available_ram()), work=work)
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
    if a.stop_file:
        r.watch_stop(a.stop_file)
    rc = r.run()
    log.close()
    if rc == 0 and a.desktop and os.name != "nt":
        print(f"Menu entry: {install_desktop(c.root)}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
