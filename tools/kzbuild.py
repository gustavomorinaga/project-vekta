#!/usr/bin/env python3
"""The build steps, in Python, so the same code runs on Linux and on Windows (the installer's own Python, CMake,
Ninja and llvm-mingw):

    tools/kzbuild.py patch        work/ps3recomp-patched: third_party/ps3recomp with port/patches/series applied
    tools/kzbuild.py lift         work/lift: the game's code as C++
    tools/kzbuild.py vfs          work/vfs: the folder the game runs from
    tools/kzbuild.py build        work/build/KZ1HD (Linux, clang) or work/build-win/KZ1HD.exe (Windows, llvm-mingw)
    tools/kzbuild.py build --win  work/build-win/KZ1HD.exe from Linux, with the pinned llvm-mingw
    tools/kzbuild.py data         once, when the game folder has only data_ps3.psarc.edat: runs the built game until it
                                  has opened the archive (work/edat-cache), checks it and takes the movies out of it
    tools/kzbuild.py env          the paths below, as the steps see them

tools/ps3recomp-patched.sh, tools/lift.sh, tools/setup-vfs.sh, tools/build.sh and tools/build-win.sh run these.
The paths are tools/env.sh's: each KZ_* variable from the environment, else work/paths.env (written by
tools/install.sh), else its default under Game/ and work/. KZ_JOBS: parallel jobs (default: every core).
KZ_PY: the Python that runs ps3recomp's lifters (default work/venv's, else this one: they need only the standard
library). KZ_DLSS_SDK (default unset: no DLSS): the NVIDIA DLSS SDK folder, as tools/release/firstrun.py's dlss step
fetches it into work/toolchain-cache/dlss-sdk after the player accepted NVIDIA's licence; the build then has DLSS and
its runtime library goes beside the program (libnvidia-ngx-dlss.so.* beside KZ1HD, nvngx_dlss.dll beside KZ1HD.exe).
Unset, the build has no DLSS and no NVIDIA file is left beside it. Exit 0 = done, 1 = a step failed (its reason on
stderr). Standard library only, Python 3.8+.

The patch step is adapted from Project Helghast's tools/ps3recomp-patched.sh (undecuple, MIT): the line-ending check
and the content-only sync are theirs; the series file is ours.
"""
import argparse
import filecmp
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WINDOWS = os.name == "nt"
PATH_VARS = ("KZ_GAME", "KZ_EBOOT", "KZ_PSARC", "KZ_WORK", "KZ_VFS", "PS3RECOMP_DIR", "KZ_PY")
# The data archive the port was made from (docs/GETTING-FILES.md): the decrypted data_ps3.psarc, which the game reads
# either as the player's own file (KZ_PSARC) or, when there is none, from the installed game's data_ps3.psarc.edat,
# decrypted by the runtime into work/edat-cache on the first open (patch 4014).
PSARC_SIZE, PSARC_SHA256 = 3147174243, "341f3c61ee14836047659ebca863ab454fff14f712301ccf8e7f9029deb65d2d"
EDAT_NAME = "data_ps3.psarc.edat"


class Fail(Exception):
    pass


# ------------------------------------------------------------------------------------------------------------ paths
def read_paths_env(path):
    """work/paths.env's `: "${VAR:=value}"` lines as {VAR: value}."""
    out = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                m = re.match(r'^: "\$\{(\w+):=(.*)\}"$', line.strip())
                if m:
                    out[m.group(1)] = re.sub(r'\\([\\"$`])', r"\1", m.group(2))
    except OSError:
        pass
    return out


def venv_bin(work):
    return os.path.join(work, "venv", "Scripts" if WINDOWS else "bin")


def resolve(environ=None, root=ROOT):
    """tools/env.sh's values: {VAR: path} (and PATH with work/venv's tools first)."""
    e = dict(os.environ if environ is None else environ)
    work0 = e.get("KZ_WORK") or os.path.join(root, "work")
    for k, v in read_paths_env(os.path.join(work0, "paths.env")).items():
        e.setdefault(k, v)
    j = os.path.join
    d = {"KZ_GAME": j(root, "Game", "NPUA80856"), "KZ_EBOOT": j(root, "Game", "EBOOT.elf"),
         "KZ_PSARC": j(root, "Game", "data_ps3.psarc"), "KZ_WORK": work0}
    for k, v in d.items():
        e[k] = e.get(k) or v
    w = e["KZ_WORK"]
    e["KZ_VFS"] = e.get("KZ_VFS") or j(w, "vfs")
    e["PS3RECOMP_DIR"] = e.get("PS3RECOMP_DIR") or j(w, "ps3recomp-patched")
    if not e.get("KZ_PY"):
        venv_py = j(venv_bin(w), "python.exe" if WINDOWS else "python")
        e["KZ_PY"] = venv_py if os.path.isfile(venv_py) else sys.executable
    e["KZ_ROOT"] = root
    if os.path.isdir(venv_bin(w)):
        e["PATH"] = venv_bin(w) + os.pathsep + e.get("PATH", "")
    return e


def archive(e):
    """("psarc", path) for the player's decrypted archive, else ("edat", path) for the installed game's .edat."""
    if os.path.isfile(e["KZ_PSARC"]):
        return "psarc", e["KZ_PSARC"]
    edat = os.path.join(e["KZ_GAME"], "USRDIR", EDAT_NAME)
    if os.path.isfile(edat):
        return "edat", edat
    raise Fail(f"no {e['KZ_PSARC']} and no {edat} (KZ_PSARC, KZ_GAME; docs/GETTING-FILES.md)")


def jobs(e):
    if e.get("KZ_JOBS"):
        return e["KZ_JOBS"]
    try:
        return str(len(os.sched_getaffinity(0)))   # what nproc counts
    except AttributeError:
        return str(os.cpu_count() or 4)


# -------------------------------------------------------------------------------------------------------- running
def run(argv, e, stdout=None, cwd=None):
    """Runs argv; stdout: None = through, "null" = dropped, "capture" = returned, or a path to write it to.
    stderr goes through. A non-zero exit raises Fail."""
    out = None
    if not os.path.dirname(argv[0]):
        # A bare name from env's PATH: on Windows a child is looked up on this process's PATH, not on env's.
        argv = [shutil.which(argv[0], path=e.get("PATH")) or argv[0]] + list(argv[1:])
    try:
        if stdout == "capture":
            r = subprocess.run(argv, env=e, cwd=cwd, stdout=subprocess.PIPE, text=True, errors="replace")
            out = r.stdout
        elif stdout == "null":
            r = subprocess.run(argv, env=e, cwd=cwd, stdout=subprocess.DEVNULL)
        elif stdout:
            with open(stdout, "w", encoding="utf-8", newline="") as f:
                r = subprocess.run(argv, env=e, cwd=cwd, stdout=f)
        else:
            sys.stdout.flush()
            r = subprocess.run(argv, env=e, cwd=cwd)
    except OSError as err:
        raise Fail(f"cannot run {argv[0]}: {err}") from None
    if r.returncode:
        raise Fail(f"{os.path.basename(argv[0])} {' '.join(os.path.basename(a) for a in argv[1:2])} failed "
                   f"(exit {r.returncode})")
    return out


def tail(text, n):
    lines = text.splitlines()
    for line in lines[-n:]:
        print(line, flush=True)


def tail_file(path, n):
    with open(path, encoding="utf-8", errors="replace") as f:
        tail(f.read(), n)


def need_file(path, what):
    if not os.path.isfile(path):
        raise Fail(f"no {what} at {path}")


# ---------------------------------------------------------------------------------------------------------- patch
def git(e):
    g = shutil.which("git", path=e.get("PATH"))
    if not g:
        raise Fail("git is needed to apply port/patches (git apply)")
    return g


def crlf_count(g, sub, e):
    """Files checked out with CRLF that are stored with LF (Git for Windows' core.autocrlf=true)."""
    try:
        r = subprocess.run([g, "-C", sub, "ls-files", "--eol"], env=e, capture_output=True, text=True,
                           errors="replace")
    except OSError:
        return 0
    return sum(1 for line in r.stdout.splitlines() if line.split()[:2] == ["i/lf", "w/crlf"])


def read_series(root):
    with open(os.path.join(root, "port", "patches", "series"), encoding="utf-8") as f:
        series = [line.rstrip("\n").rstrip("\r") for line in f]
    series = [s for s in series if s.strip() and not s.startswith("#")]
    pdir = os.path.join(root, "port", "patches")
    for p in sorted(os.listdir(pdir)):
        if p.endswith(".patch") and not p.startswith(".") and p not in series:
            raise Fail(f"port/patches/{p} is not in port/patches/series")
    return series


def patchset_id(root, g, e):
    """The patch set a tree was made from: sha256 of the series and every patch (in name order), and the pin."""
    pdir = os.path.join(root, "port", "patches")
    h = hashlib.sha256()
    for name in ["series"] + sorted(p for p in os.listdir(pdir) if p.endswith(".patch") and not p.startswith(".")):
        with open(os.path.join(pdir, name), "rb") as f:
            h.update(f.read())
    sub = os.path.join(root, "third_party", "ps3recomp")
    pin = None
    if g:
        r = subprocess.run([g, "-C", sub, "rev-parse", "--short", "HEAD"], env=e, capture_output=True, text=True)
        if r.returncode == 0:
            pin = r.stdout.strip()
    if pin is None:
        try:
            with open(sub + ".pin", encoding="utf-8") as f:
                pin = "\n".join(line[:7] for line in f.read().rstrip("\n").split("\n"))
        except OSError:
            pin = "unknown"
    return f"{h.hexdigest()[:16]}-{pin}"


def sync_tree(new, pt):
    """Copies over only the files whose content changed (an unchanged file keeps its mtime, so the build recompiles
    only what the patch set changed) and removes the ones the new tree no longer has."""
    changed = removed = 0
    for d, _, files in os.walk(new):
        rel = os.path.relpath(d, new)
        os.makedirs(os.path.join(pt, rel), exist_ok=True)
        for f in files:
            a, b = os.path.join(d, f), os.path.join(pt, rel, f)
            if not os.path.exists(b) or not filecmp.cmp(a, b, shallow=False):
                shutil.copyfile(a, b)
                shutil.copymode(a, b)
                changed += 1
    for d, _, files in os.walk(pt):
        rel = os.path.relpath(d, pt)
        for f in files:
            if rel == "." and f == ".patchset":
                continue
            if not os.path.exists(os.path.join(new, rel, f)):
                os.remove(os.path.join(d, f))
                removed += 1
    return changed, removed


def cmd_patch(e, a):
    root = e["KZ_ROOT"]
    sub = os.path.join(root, "third_party", "ps3recomp")
    pt = os.path.abspath(e["PS3RECOMP_DIR"])
    if not os.path.isfile(os.path.join(sub, "CMakeLists.txt")):
        raise Fail("third_party/ps3recomp is empty; run: git submodule update --init")
    g = git(e)
    ge = dict(e, LC_ALL="C")   # git's messages are matched below
    n = crlf_count(g, sub, ge)
    if n:
        raise Fail(f"{n} file(s) in third_party/ps3recomp are checked out with Windows (CRLF) line endings but "
                   "stored with LF, so the patches would not apply. Turn core.autocrlf off for this repository and "
                   "the submodule, then write the submodule's files out again (this discards any edits in it):\n"
                   "  git config core.autocrlf false\n"
                   "  git -C third_party/ps3recomp config core.autocrlf false\n"
                   "  git -C third_party/ps3recomp rm -r -q --cached . && git -C third_party/ps3recomp reset -q --hard")
    series = read_series(root)
    # Patch a scratch copy (no git metadata), then sync it into the tree.
    new = pt + ".new"
    shutil.rmtree(new, ignore_errors=True)
    shutil.copytree(sub, new, symlinks=True, ignore=shutil.ignore_patterns(".git"))
    inside = os.path.normcase(new).startswith(os.path.normcase(os.path.abspath(root)) + os.sep)
    for name in series:
        p = os.path.join(root, "port", "patches", name)
        if not os.path.isfile(p):
            raise Fail(f"series names {name}, which is not in port/patches/")
        # Inside the repository, --directory from its root: a bare git apply in the copy would skip paths outside
        # it. Outside it (PS3RECOMP_DIR elsewhere), from the copy itself.
        if inside:
            rel = os.path.relpath(new, root).replace(os.sep, "/")
            argv, cwd = [g, "apply", "--verbose", "--whitespace=nowarn", f"--directory={rel}", p], root
        else:
            argv, cwd = [g, "apply", "--verbose", "--whitespace=nowarn", p], new
        r = subprocess.run(argv, env=ge, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           errors="replace")
        if r.returncode:
            raise Fail(r.stdout.rstrip())
        if not re.search(r"^Applied patch .* cleanly", r.stdout, re.M):
            raise Fail(f"patch port/patches/{name} did not apply: {r.stdout.rstrip()}")
    print(f"applied {len(series)} patches", flush=True)
    os.makedirs(pt, exist_ok=True)
    changed, removed = sync_tree(new, pt)
    print(f"synced: {changed} file(s) changed, {removed} removed", flush=True)
    shutil.rmtree(new)
    with open(os.path.join(pt, ".patchset"), "w", encoding="utf-8", newline="\n") as f:
        f.write(patchset_id(root, g, ge) + "\n")


# ----------------------------------------------------------------------------------------------------------- lift
def native_names(folder, ext, pattern):
    """The functions a native folder reimplements (the lifters emit them as <name>_lifted), comma-separated."""
    rx = re.compile(pattern, re.M)
    names = []
    try:
        files = sorted(f for f in os.listdir(folder) if f.endswith(ext))
    except OSError:
        files = []
    for f in files:
        with open(os.path.join(folder, f), encoding="utf-8", errors="replace") as fh:
            names += rx.findall(fh.read())
    return ",".join(names)


def read_seeds(path):
    with open(path, encoding="utf-8") as f:
        return [line.strip() for line in f if not line.startswith("#") and line.strip()]


def cmd_lift(e, a):
    root, py = e["KZ_ROOT"], e["KZ_PY"]
    j = os.path.join
    t = j(e["PS3RECOMP_DIR"], "tools")
    p = j(root, "port", "kz1hd")
    k = j(e["KZ_WORK"], "lift")
    eboot = e["KZ_EBOOT"]
    if not os.path.isfile(j(t, "ppu_lifter.py")):
        raise Fail(f"no {j(t, 'ppu_lifter.py')}: run tools/ps3recomp-patched.sh first")
    need_file(eboot, "EBOOT (KZ_EBOOT; docs/GETTING-FILES.md)")
    os.makedirs(j(k, "analysis"), exist_ok=True)
    an = j(k, "analysis") + os.sep
    # The lifters write text in the locale's encoding: UTF-8 everywhere (on Windows they would use the code page;
    # their line endings stay the system's, which clang reads the same).
    e["PYTHONUTF8"] = "1"

    print("== PPU", flush=True)
    # The loader's view of the EBOOT (segments, imports, the functions its tables name), then the function list
    # from those and the seeds in re/function_seeds.txt.
    run([py, j(t, "ppu_loader.py"), eboot, "-o", an], e, "null")
    with open(j(k, "analysis", "seeds.json"), "w", encoding="utf-8") as f:
        json.dump(read_seeds(j(root, "re", "function_seeds.txt")), f)
    run([py, j(t, "find_functions.py"), eboot, "--seed-json", j(k, "analysis", "EBOOT.functions.json"),
         "--seed-json", j(k, "analysis", "seeds.json"), "--output", j(k, "analysis", "functions.json")], e, "null")
    # Functions port/kz1hd/native_ppu/ reimplements: the lifter emits them as <name>_lifted.
    e["PPU_LIFT_NATIVE"] = native_names(j(p, "native_ppu"), ".cpp", r"^void (func_[0-9A-F]*)\(ppu_context\* ctx\)$")
    # The build globs the folder's sources: a chunk an earlier lift left there (another version's chunk count) would be
    # compiled in, so each lift starts from an empty folder, as the SPU one does.
    shutil.rmtree(j(k, "gen"), ignore_errors=True)
    # 0x3B5CB0/0x3B5DD4: its libc setjmp/longjmp, which Lua 4.0 raises its errors with.
    run([py, j(t, "ppu_lifter.py"), eboot, "--functions", j(k, "analysis", "functions.json"), "--hle-stubs",
         j(k, "analysis", "EBOOT.imports.json"), "--setjmp", "0x3B5CB0", "--longjmp", "0x3B5DD4", "--code-end",
         "0x52f740", "--chunk-lines", "150000", "-j", jobs(e), "--output", j(k, "gen") + os.sep], e, j(k, "lift.log"))
    tail_file(j(k, "lift.log"), 2)
    # Guest registers in host locals inside each function (see the tool's header): ~+7% in Level 1 at 144 Hz.
    tail(run([py, j(t, "ppu_regcache.py"), j(k, "gen")], e, "capture"), 1)

    print("== SPU", flush=True)
    # Functions port/kz1hd/native/ reimplements: the lifter emits them as <name>_lifted.
    e["SPU_LIFT_NATIVE"] = native_names(j(p, "native"), ".c",
                                        r"^void ([a-z0-9_]*_spu_func_[0-9A-F]*)\(spu_context\* ctx\)$")
    s = j(k, "spu")
    shutil.rmtree(s, ignore_errors=True)
    os.makedirs(j(s, "images"))
    run([py, j(t, "extract_spu_images.py"), eboot, "--output", j(s, "images")], e, "null")
    tail(run([py, j(root, "tools", "extract_spu_modules.py"), eboot, "--out", s, "--pm", "0x5B9180:12160",
              "--ps3recomp", e["PS3RECOMP_DIR"]], e, "capture"), 1)
    tail(run([py, j(t, "lift_jobmods.py"), "--jobmods", j(s, "jobmods"), "--out", s, "--register",
              j(s, "jobmods_register.c"), "--symbol-prefix", "kz1"], e, "capture"), 1)
    tail(run([py, j(t, "build_spu_workloads.py"), "--images", j(s, "images"), "--lifted", s, "--out",
              j(s, "spu_workloads.c"), "--register-fn", "kz1_spu_register_all", "--constructor", "--title", "kz1hd"],
             e, "capture"), 1)

    # Up to 0.5.x the firmware's MP3 decoder was lifted here; port/kz1hd/native/flashmp3.c answers its call now.
    for old in ("flashmp3", "flashmp3_src"):
        shutil.rmtree(j(s, old), ignore_errors=True)

    # work/fw held the firmware libsre lifted by the port before 0.6.0; the SPURS queue it ran is in the runtime now.
    shutil.rmtree(j(e["KZ_WORK"], "fw"), ignore_errors=True)


# ------------------------------------------------------------------------------------------------------------ vfs
def link_or_copy(src, dst):
    """dst -> src: a symbolic link, else (Windows without the right to make one) a hard link, else a copy."""
    src = os.path.realpath(src)
    if os.path.lexists(dst):
        if not os.path.islink(dst) and os.path.isfile(dst) and os.path.samefile(src, dst):
            return "kept"
        os.remove(dst)
    try:
        os.symlink(src, dst)
        return "link"
    except OSError:
        pass
    try:
        os.link(src, dst)
        return "hard link"
    except OSError:
        shutil.copy2(src, dst)
        return "copy"


def cmd_vfs(e, a):
    j = os.path.join
    game, vfs = e["KZ_GAME"], e["KZ_VFS"]
    for f in (j(game, "PARAM.SFO"), e["KZ_EBOOT"]):
        if not os.path.isfile(f):
            raise Fail(f"no {f} (KZ_GAME, KZ_EBOOT; docs/GETTING-FILES.md)")
    kind, src = archive(e)
    u = j(vfs, "PS3_GAME", "USRDIR")
    os.makedirs(u, exist_ok=True)
    shutil.copyfile(j(game, "PARAM.SFO"), j(vfs, "PS3_GAME", "PARAM.SFO"))
    if os.path.isdir(j(game, "TROPDIR")):
        shutil.copytree(j(game, "TROPDIR"), j(vfs, "PS3_GAME", "TROPDIR"), dirs_exist_ok=True)
    else:
        print(f"no TROPDIR in {game}: trophies are logged without their names", flush=True)
    # The game opens data_ps3.psarc and, when there is none, data_ps3.psarc.edat: only one of them is placed.
    name = "data_ps3.psarc" if kind == "psarc" else EDAT_NAME
    other = j(u, EDAT_NAME if kind == "psarc" else "data_ps3.psarc")
    if os.path.lexists(other):
        os.remove(other)
    how = {link_or_copy(e["KZ_EBOOT"], j(u, "EBOOT.elf")), link_or_copy(src, j(u, name))}
    if "copy" in how:
        print(f"note: no links on this file system: EBOOT.elf and {name} were copied into the game folder", flush=True)
    if kind == "psarc":
        extract_movies(e, src, u)
    else:
        dec = prepared_archive(e)
        if dec:
            extract_movies(e, dec, u)
        else:
            print(f"vfs: {vfs} (the movies come out of the archive with `kzbuild.py data`, after the build)", flush=True)


def extract_movies(e, psarc, u):
    run([sys.executable, os.path.join(e["KZ_ROOT"], "tools", "psarc_ls.py"), psarc, u, ".avi"], e, "null")
    n = sum(1 for _, _, fs in os.walk(u) for f in fs if f.endswith(".avi"))
    print(f"vfs: {e['KZ_VFS']} ({n} movies)", flush=True)


def edat_cache(e):
    """Where the runtime keeps the archive's plaintext: $PS3_EDAT_CACHE, else work/edat-cache (main.cpp's default,
    the VFS root's sibling)."""
    return e.get("PS3_EDAT_CACHE") or os.path.join(os.path.dirname(os.path.abspath(e["KZ_VFS"])), "edat-cache")


def prepared_archive(e):
    """The cached plaintext of data_ps3.psarc.edat when an earlier `data` step checked it, else None."""
    ok = os.path.join(edat_cache(e), "data_ps3.psarc.ok")
    try:
        with open(ok, encoding="utf-8") as f:
            dec = f.read().strip()
    except OSError:
        return None
    return dec if os.path.isfile(dec) and os.path.getsize(dec) == PSARC_SIZE else None


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 22), b""):
            h.update(block)
    return h.hexdigest()


def cmd_data(e, a):
    if archive(e)[0] == "psarc":
        print("data: the decrypted data_ps3.psarc is used as it is; nothing to prepare", flush=True)
        return
    u = os.path.join(e["KZ_VFS"], "PS3_GAME", "USRDIR")
    if prepared_archive(e) and any(f.endswith(".avi") for _, _, fs in os.walk(u) for f in fs):
        print("data: already prepared", flush=True)
        return
    sys.path.insert(0, os.path.join(ROOT, "tools", "release"))
    import launch   # noqa: E402  (tools/release imports this module: import it only here)
    argv, _, env, _ = launch.plan(e["KZ_ROOT"], e)
    log = os.path.join(e["KZ_WORK"], "prepare-data.log")
    # The run folder, not the saves folder: the game stops at the archive's first open, before it reads a save.
    env.update(KZ_PREPARE_DATA="1", PS3RECOMP_VK_WINDOW="0", KZ_LOG=log, PS3_VFS_ROOT=e["KZ_VFS"].replace("\\", "/"))
    secs = int(e.get("KZ_DATA_TIMEOUT") or 900)   # 36 s on an NVMe drive; a slow hard disk takes several minutes
    print(f"data: opening the game's archive once ({log})", flush=True)
    try:
        with open(log, "w", encoding="utf-8", errors="replace") as f:
            rc = subprocess.run(argv, env=env, cwd=e["KZ_VFS"], stdin=subprocess.DEVNULL, stdout=f,
                                stderr=subprocess.STDOUT, timeout=secs).returncode
    except subprocess.TimeoutExpired:
        raise Fail(f"the game had not opened its archive after {secs} s (KZ_DATA_TIMEOUT); see {log}")
    with open(log, encoding="utf-8", errors="replace") as f:
        text = f.read()
    m = re.search(r"^\[kz\] data ready: (.+)$", text, re.M)
    if not m:
        raise Fail(f"the game stopped (exit {rc}) before it opened its archive; the last lines of {log}:\n"
                   + "\n".join(text.splitlines()[-12:]))
    dec = m.group(1).strip()
    size = os.path.getsize(dec) if os.path.isfile(dec) else -1
    if size != PSARC_SIZE or sha256_file(dec) != PSARC_SHA256:
        raise Fail(f"the opened archive {dec} is not the tested one ({size} bytes; docs/GETTING-FILES.md); "
                   "is the installed game NPUA80856 version 01.00?")
    with open(os.path.join(edat_cache(e), "data_ps3.psarc.ok"), "w", encoding="utf-8") as f:
        f.write(dec + "\n")
    extract_movies(e, dec, u)


# ---------------------------------------------------------------------------------------------------------- build
def lift_defs(e):
    w = e["KZ_WORK"]
    if not os.path.isfile(os.path.join(w, "lift", "gen", "ppu_recomp.h")):
        raise Fail(f"no lift in {os.path.join(w, 'lift')}: run tools/lift.sh first")
    return [f"-DPS3RECOMP_DIR={e['PS3RECOMP_DIR']}", f"-DRECOMP_DIR={os.path.join(w, 'lift', 'gen')}",
            f"-DSPU_DIR={os.path.join(w, 'lift', 'spu')}"]


def ninja(e, b):
    run(["ninja", "-C", b] + (["-j", e["KZ_JOBS"]] if e.get("KZ_JOBS") else []), e)


def march(e):
    """The -march of the build (KZ_MARCH, default x86-64-v3): the Linux and Windows builds both pass it to CMake."""
    return e.get("KZ_MARCH") or "x86-64-v3"


def cpu_required(m):
    """The x86-64 level port/kz1hd/cpu_check.c checks for with -march=m (CMakeLists.txt's rule): 2, 3 or 4; 0 for
    any other -march (no check)."""
    r = re.fullmatch(r"x86-64-v([234])", m or "")
    return int(r.group(1)) if r else 0


def build_linux(e):
    """Linux build of the port: clang, Ninja and the Vulkan renderer. KZ_MARCH sets -march (default x86-64-v3).
    KZ_LINUX_TOOLCHAIN=pinned (the Linux release folder's run.sh sets it) builds with the pinned toolchain instead of
    the system's: build_linux_pinned."""
    if e.get("KZ_LINUX_TOOLCHAIN") == "pinned":
        return build_linux_pinned(e)
    b = e.get("KZ_BUILD") or os.path.join(e["KZ_WORK"], "build")
    defs = lift_defs(e)
    # -O1 for the base flags, as in the maintainer's build; the lifted PPU code gets KZ_PPU_OPT (-O2) on its own.
    run(["cmake", "-G", "Ninja", "-S", os.path.join(e["KZ_ROOT"], "port", "kz1hd"), "-B", b,
         "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_C_FLAGS=-O1", "-DCMAKE_CXX_FLAGS=-O1",
         f"-DKZ_MARCH={march(e)}", "-DPS3RECOMP_RSX_VULKAN=ON", *dlss_defs(e), *defs], e, "null")
    ninja(e, b)
    place_dlss(e, b)
    print(f"build: {os.path.join(b, 'KZ1HD')}", flush=True)


# The NGX library each build links and the DLSS runtime library it puts beside the program (the SDK's own paths; NGX
# looks in the program's folder).
DLSS_FILES = {
    "linux": (("lib", "Linux_x86_64", "libnvsdk_ngx.a"), ("lib", "Linux_x86_64", "rel"),
              re.compile(r"^libnvidia-ngx-dlss\.so(\.[0-9.]+)?$")),
    "windows": (("lib", "Windows_x86_64", "x64", "nvsdk_ngx_s.lib"), ("lib", "Windows_x86_64", "rel"),
                re.compile(r"^nvngx_dlss\.dll$", re.I)),
}


def dlss_defs(e, target="linux"):
    """-DPS3RECOMP_DLSS_SDK from KZ_DLSS_SDK; empty when unset, so a build reconfigured after a decline drops it."""
    sdk = e.get("KZ_DLSS_SDK") or ""
    lib = os.path.join(*DLSS_FILES[target][0])
    if sdk and not os.path.isfile(os.path.join(sdk, lib)):
        raise Fail(f"KZ_DLSS_SDK={sdk} is not the DLSS SDK (no {lib.replace(os.sep, '/')})")
    return [f"-DPS3RECOMP_DLSS_SDK={sdk}"]


def place_dlss(e, b, target="linux"):
    """The DLSS runtime library beside the program when the build has DLSS; otherwise none is left there."""
    _, runtime, rx = DLSS_FILES[target]
    sdk = e.get("KZ_DLSS_SDK") or ""
    src = os.path.join(sdk, *runtime) if sdk else ""
    want = {n for n in os.listdir(src) if rx.match(n)} if src and os.path.isdir(src) else set()
    if not os.path.isdir(b):
        return
    for n in os.listdir(b):
        if rx.match(n) and n not in want:
            os.remove(os.path.join(b, n))
    for n in sorted(want):
        copy_newer(os.path.join(src, n), os.path.join(b, n))


LINUX_PINNED = ("zig", "cmake-linux", "ninja-linux", "sdl2-src", "ffmpeg-linux", "zlib", "glslang", "vulkan-headers")


def build_linux_pinned(e):
    """KZ1HD with the pinned Linux toolchain (tools/release/toolchain.json, host native): Zig as the compiler for
    x86_64-linux-gnu.2.31 (tools/release/zig/, port/cmake/zig-linux.cmake), the pinned CMake and Ninja, and the
    libraries build_linux_deps.py builds; nothing from the system but Python. The program needs only glibc 2.31 or
    newer, the system's libSDL2-2.0.so.0 and a Vulkan driver; the pinned FFmpeg goes to <build>/lib, which
    tools/play.sh puts on LD_LIBRARY_PATH."""
    root, w, j = e["KZ_ROOT"], e["KZ_WORK"], os.path.join
    b = e.get("KZ_BUILD") or j(w, "build")
    rel = j(root, "tools", "release")
    cache, deps = j(w, "toolchain-cache"), j(w, "linux-deps")
    run([sys.executable, j(rel, "fetch_toolchain.py"), "--only", ",".join(LINUX_PINNED)], e, "null")
    run([sys.executable, j(rel, "build_linux_deps.py"), "--cache", cache, "--prefix", deps], e)
    e["PATH"] = os.pathsep.join([j(cache, "cmake-linux", "bin"), j(cache, "ninja-linux"), e.get("PATH", "")])
    e["KZ_ZIG"] = j(cache, "zig", "zig")
    run(["cmake", "-G", "Ninja", "-S", j(root, "port", "kz1hd"), "-B", b,
         f"-DCMAKE_TOOLCHAIN_FILE={j(root, 'port', 'cmake', 'zig-linux.cmake')}", f"-DKZ_LINUX_DEPS={deps}",
         "-DCMAKE_C_FLAGS=-O1", "-DCMAKE_CXX_FLAGS=-O1", f"-DKZ_MARCH={march(e)}",
         "-DPS3RECOMP_RSX_VULKAN=ON", f"-DPS3RECOMP_VULKAN_INCLUDE_DIR={j(deps, 'include')}",
         f"-DKZ_FFMPEG_INCLUDE_DIR={j(deps, 'include')}", f"-DPython3_EXECUTABLE={sys.executable}",
         # the link-only SDL2 in linux-deps must never be found at run time: no build RPATH
         "-DCMAKE_SKIP_BUILD_RPATH=ON", *dlss_defs(e), *lift_defs(e)], e, "null")
    ninja(e, b)
    place_dlss(e, b)
    lib = j(b, "lib")
    os.makedirs(lib, exist_ok=True)
    for f in sorted(os.listdir(j(deps, "ffmpeg"))):
        src, dst = j(deps, "ffmpeg", f), j(lib, f)
        if os.path.islink(src):
            if os.path.lexists(dst):
                os.remove(dst)
            os.symlink(os.readlink(src), dst)
        else:
            copy_newer(src, dst)
    print(f"build: {j(b, 'KZ1HD')}", flush=True)


FFMPEG_HEADERS = ("libavcodec", "libavformat", "libavutil", "libswscale", "libswresample")


def copy_newer(src, dst):
    if not os.path.exists(dst) or os.path.getmtime(src) > os.path.getmtime(dst):
        shutil.copy2(src, dst)


def build_windows(e):
    """KZ1HD.exe with the pinned toolchain (tools/release/toolchain.json): llvm-mingw, SDL2, zlib, glslang, the
    Vulkan headers and FFmpeg's LGPL build, fetched and checked by fetch_toolchain.py, the libraries built and staged
    by build_win_deps.py (both do nothing when that is done). On Windows, CMake and Ninja come from the same cache."""
    root, w, j = e["KZ_ROOT"], e["KZ_WORK"], os.path.join
    b = e.get("KZ_BUILD") or j(w, "build-win")
    defs = lift_defs(e)
    rel = j(root, "tools", "release")
    cache = j(w, "toolchain-cache")
    deps = j(w, "win-deps")
    llvm = "llvm-mingw" if WINDOWS else "llvm-mingw-linux"
    tools = [llvm, "cmake", "ninja"] if WINDOWS else [llvm]
    if WINDOWS:   # the cache's CMake and Ninja, before any on PATH
        e["PATH"] = os.pathsep.join([j(cache, "cmake", "bin"), j(cache, "ninja"), e.get("PATH", "")])
    run([sys.executable, j(rel, "fetch_toolchain.py"), "--only",
         ",".join(tools + ["sdl2", "zlib", "glslang", "vulkan-headers"])], e, "null")
    # FFmpeg (served from this repository's deps-ffmpeg-* release): when it cannot be fetched (offline), the host's
    # headers and no DLLs.
    try:
        run([sys.executable, j(rel, "fetch_toolchain.py"), "--only", "ffmpeg"], e, "null")
    except Fail:
        print("note: FFmpeg's pinned build could not be fetched: the host's headers, no movie DLLs", flush=True)
    run([sys.executable, j(rel, "build_win_deps.py"), "--cache", cache, "--prefix", deps, "--llvm-mingw",
         j(cache, llvm)], e)
    ff = j(deps, "include")
    if not os.path.isfile(j(ff, "libavcodec", "avcodec.h")):
        ff = j(w, "win-headers")
        if not os.path.isdir(j(ff, "libavcodec")):
            if WINDOWS or not os.path.isdir("/usr/include/libavcodec"):
                raise Fail("no FFmpeg headers: fetch the pinned FFmpeg (tools/release/fetch_toolchain.py --only "
                           "ffmpeg; docs/dev/deps-mirror.md)")
            for h in FFMPEG_HEADERS:
                shutil.copytree(j("/usr/include", h), j(ff, h), dirs_exist_ok=True)
    run(["cmake", "-G", "Ninja", "-S", j(root, "port", "kz1hd"), "-B", b,
         f"-DCMAKE_TOOLCHAIN_FILE={j(root, 'port', 'cmake', 'llvm-mingw.cmake')}", f"-DLLVM_MINGW_ROOT={j(cache, llvm)}",
         f"-DKZ_WIN_DEPS={deps}", "-DCMAKE_BUILD_TYPE=Release", f"-DKZ_MARCH={march(e)}", *defs, "-DPS3RECOMP_RSX_VULKAN=ON",
         f"-DPS3RECOMP_VULKAN_INCLUDE_DIR={j(deps, 'include')}", f"-DKZ_FFMPEG_INCLUDE_DIR={ff}",
         # the build's generators (gen_hle_nids.py and ps3recomp's): this Python, the bundled one on Windows,
         # which is neither registered nor on PATH
         f"-DPython3_EXECUTABLE={sys.executable}", *dlss_defs(e, "windows")], e, "null")
    ninja(e, b)
    place_dlss(e, b, "windows")
    # SDL2 and the FFmpeg libraries the movie loader opens (not avdevice or avfilter).
    dll_dir = j(deps, "bin")
    for d in sorted(os.listdir(dll_dir)) if os.path.isdir(dll_dir) else []:
        if d.endswith(".dll") and not d.startswith(("avdevice-", "avfilter-")):
            copy_newer(j(dll_dir, d), j(b, d))
    if not any(f.startswith("avcodec-") and f.endswith(".dll") for f in os.listdir(b)):
        print(f"note: no FFmpeg DLLs in {b} (the movies are skipped)", flush=True)
    print(f"built: {j(b, 'KZ1HD.exe')}", flush=True)


def cmd_build(e, a):
    if a.win or WINDOWS:
        build_windows(e)
    else:
        build_linux(e)


def cmd_env(e, a):
    for k in PATH_VARS:
        print(f"{k}={e[k]}")


# ----------------------------------------------------------------------------------------------------------- main
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sp = ap.add_subparsers(dest="cmd", required=True)
    for name in ("patch", "lift", "vfs", "data", "env"):
        sp.add_parser(name)
    bp = sp.add_parser("build")
    bp.add_argument("--win", action="store_true", help="the Windows exe, cross-built with the pinned llvm-mingw")
    a = ap.parse_args(argv)
    for s in (sys.stdout, sys.stderr):   # a tool's message in another code page must not stop the step
        if hasattr(s, "reconfigure"):
            s.reconfigure(errors="replace")
    e = resolve()
    try:
        {"patch": cmd_patch, "lift": cmd_lift, "vfs": cmd_vfs, "data": cmd_data, "build": cmd_build, "env": cmd_env}[a.cmd](e, a)
    except Fail as err:
        print(f"kzbuild {a.cmd}: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
