#!/usr/bin/env python3
"""Host test for tools/kzbuild.py, the build steps tools/ps3recomp-patched.sh, tools/lift.sh, tools/setup-vfs.sh and
tools/build.sh run (and the installer runs on Windows): the paths come from the environment, else work/paths.env,
else the defaults, as in tools/env.sh; patch applies the series to a copy, rewrites only the files that changed and
writes .patchset, and refuses a patch left out of the series; lift calls ps3recomp's lifters (stand-ins here) with
the arguments and native-function lists of the shell script it replaced, and writes seeds.json and libsre's
--hle-stubs file; vfs copies PARAM.SFO and TROPDIR, links the EBOOT and the archive, takes the movies out of a small
PSARC and keeps the saves; build configures and runs Ninja (stand-ins on PATH). No game data, no compiler."""
import io
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import kzbuild  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def make_psarc(path, files):
    """A PSARC in the layout tools/psarc_ls.py reads: entry 0 is the name list, one zlib block per file."""
    names = list(files)
    blobs = [zlib.compress("\n".join(names).encode())] + [zlib.compress(files[n]) for n in names]
    sizes = [len("\n".join(names).encode())] + [len(files[n]) for n in names]
    n, esz = len(blobs), 30
    toclen = 32 + esz * n + 2 * n
    off, entries = toclen, b""
    for i, (b, ln) in enumerate(zip(blobs, sizes)):
        entries += b"\0" * 16 + struct.pack(">I", i) + ln.to_bytes(5, "big") + off.to_bytes(5, "big")
        off += len(b)
    with open(path, "wb") as f:
        f.write(struct.pack(">4sI4sIIIII", b"PSAR", 0x10004, b"zlib", toclen, esz, n, 65536, 2))
        f.write(entries + struct.pack(f">{n}H", *(len(b) for b in blobs)) + b"".join(blobs))


STUB = r"""import json, os, sys
name = os.path.basename(sys.argv[0])
with open(os.path.join(os.environ["KZ_WORK"], "argv.log"), "a") as f:
    f.write(json.dumps([name, sys.argv[1:], os.environ.get("PPU_LIFT_NATIVE"), os.environ.get("SPU_LIFT_NATIVE")])
            + "\n")
if name == "lift_prx.py":
    out = sys.argv[sys.argv.index("--output") + 1]
    os.makedirs(out, exist_ok=True)
    json.dump({"cellSpurs": {"0x1": "a", "0x2": "b"}, "sysPrx": {"0x3": "c"}},
              open(os.path.join(out, "libsre_imports.json"), "w"))
for i in range(3):
    print(name + " line " + str(i))
if name == "spu_lifter.py":
    print("12 function(s) lifted")
"""

base = {k: v for k, v in os.environ.items() if not k.startswith(("KZ_", "PS3"))}

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)

    # paths: the defaults, work/paths.env, the environment first
    root = tmp / "tree"
    (root / "work").mkdir(parents=True)
    e = kzbuild.resolve(base, root=str(root))
    check(e["KZ_EBOOT"] == str(root / "Game" / "EBOOT.elf") and e["KZ_VFS"] == str(root / "work" / "vfs"),
          f"the defaults are under Game/ and work/: {e['KZ_EBOOT']} {e['KZ_VFS']}")
    check(e["KZ_PY"] == sys.executable, "without work/venv the lifters run with this Python")
    odd = str(tmp / 'my "files" $x')
    (root / "work" / "paths.env").write_text('# a comment\n: "${KZ_EBOOT:=' + odd.replace('"', '\\"').replace("$", "\\$")
                                             + '}"\n')
    check(kzbuild.resolve(base, root=str(root))["KZ_EBOOT"] == odd, "work/paths.env is read, odd characters and all")
    check(kzbuild.resolve(dict(base, KZ_EBOOT="/mine"), root=str(root))["KZ_EBOOT"] == "/mine",
          "a variable set in the environment wins over paths.env")

    # patch
    sub = root / "third_party" / "ps3recomp"
    (sub / "tools").mkdir(parents=True)
    (sub / "CMakeLists.txt").write_text("project(x)\n")
    (sub / "tools" / "a.py").write_text("one\ntwo\n")
    (sub / "tools" / "keep.py").write_text("same\n")
    (sub / ".git").write_text("gitdir: elsewhere\n")
    (root / "third_party" / "ps3recomp.pin").write_text("4e8f3ca8abcdef\n")
    pd = root / "port" / "patches"
    pd.mkdir(parents=True)
    (pd / "1001-a.patch").write_text("--- a/tools/a.py\n+++ b/tools/a.py\n@@ -1,2 +1,2 @@\n one\n-two\n+three\n")
    (pd / "series").write_text("# order\n1001-a.patch\n")
    env = dict(base, KZ_ROOT=str(root))
    pt = root / "work" / "ps3recomp-patched"
    e = kzbuild.resolve(env, root=str(root))
    try:
        kzbuild.cmd_patch(e, None)
    except kzbuild.Fail as err:
        check(False, f"patch applies: {err}")
    check((pt / "tools" / "a.py").read_text() == "one\nthree\n" and not (pt / ".git").exists()
          and not (root / "work" / "ps3recomp-patched.new").exists(), "the series is applied to a copy, without .git")
    import hashlib
    want = hashlib.sha256((pd / "series").read_bytes() + (pd / "1001-a.patch").read_bytes()).hexdigest()[:16]
    check((pt / ".patchset").read_text() == want + "-4e8f3ca\n", f"the .patchset: {(pt / '.patchset').read_text()!r}")
    os.utime(pt / "tools" / "keep.py", (1, 1))
    kzbuild.cmd_patch(e, None)
    check(os.path.getmtime(pt / "tools" / "keep.py") == 1, "an unchanged file is not rewritten")
    (pd / "1002-b.patch").write_text("")
    try:
        kzbuild.cmd_patch(e, None)
        check(False, "a patch not in the series is refused")
    except kzbuild.Fail as err:
        check("not in port/patches/series" in str(err), str(err))
    (pd / "1002-b.patch").unlink()

    # lift, with stand-ins for ps3recomp's lifters
    for t in ("ppu_loader", "find_functions", "ppu_lifter", "ppu_regcache", "extract_spu_images", "lift_jobmods",
              "build_spu_workloads", "rebase_spu_elf", "spu_lifter", "lift_prx"):
        (pt / "tools" / f"{t}.py").write_text(STUB)
    (root / "tools").mkdir()
    (root / "tools" / "extract_spu_modules.py").write_text(STUB)
    (root / "re").mkdir()
    (root / "re" / "function_seeds.txt").write_text("# seeds\n0x10\n\n  0x20  \n")
    for d, f, text in [("native_ppu", "b.cpp", "void func_00AB(ppu_context* ctx)\n{\n}\nvoid func_00CD(int)\n"),
                       ("native_ppu", "a.cpp", "void func_0012(ppu_context* ctx)\n"),
                       ("native", "x.c", "void ab05_spu_func_01F0(spu_context* ctx)\nstatic void y(void)\n")]:
        (root / "port" / "kz1hd" / d).mkdir(parents=True, exist_ok=True)
        (root / "port" / "kz1hd" / d / f).write_text(text)
    files = tmp / "files"
    (files / "dev").mkdir(parents=True)
    for f in ("EBOOT.elf", "libsre.prx"):
        (files / f).write_text(f)
    env = dict(base, KZ_EBOOT=str(files / "EBOOT.elf"), KZ_LIBSRE=str(files / "libsre.prx"), KZ_JOBS="5",
               PS3_DEV_FLASH=str(files / "dev"))
    e = kzbuild.resolve(env, root=str(root))
    try:
        kzbuild.cmd_lift(e, None)
    except kzbuild.Fail as err:
        check(False, f"lift runs: {err}")
    calls = [json.loads(line) for line in (root / "work" / "argv.log").read_text().splitlines()]
    check([c[0] for c in calls] == ["ppu_loader.py", "find_functions.py", "ppu_lifter.py", "ppu_regcache.py",
                                    "extract_spu_images.py", "extract_spu_modules.py", "lift_jobmods.py",
                                    "build_spu_workloads.py", "lift_prx.py", "ppu_lifter.py"],
          f"the lifters run in lift.sh's order (no flashMP3.pic: no MP3 lift): {[c[0] for c in calls]}")
    ppu = calls[2]
    check(ppu[2] == "func_0012,func_00AB" and ppu[3] is None, f"PPU_LIFT_NATIVE in file order, then line: {ppu[2:]}")
    check(ppu[1][ppu[1].index("-j") + 1] == "5" and "--setjmp" in ppu[1], f"the PPU lift's arguments: {ppu[1]}")
    check(calls[5][3] == "ab05_spu_func_01F0" and calls[5][2] == "func_0012,func_00AB", "SPU_LIFT_NATIVE from SPU on")
    check(json.loads((root / "work" / "lift" / "analysis" / "seeds.json").read_text()) == ["0x10", "0x20"],
          "seeds.json: the seed lines without comments or blanks")
    stubs = json.loads((root / "work" / "fw" / "out" / "libsre_hle_stubs.json").read_text())
    check(stubs == [{"library": "cellSpurs", "nid": "0x1", "stub": "a"}, {"library": "cellSpurs", "nid": "0x2",
                     "stub": "b"}, {"library": "sysPrx", "nid": "0x3", "stub": "c"}], f"libsre's --hle-stubs: {stubs}")
    (files / "dev" / "sys" / "external").mkdir(parents=True)
    (files / "dev" / "sys" / "external" / "flashMP3.pic").write_text("mp3")
    (root / "work" / "argv.log").unlink()
    kzbuild.cmd_lift(e, None)
    names = [json.loads(line)[0] for line in (root / "work" / "argv.log").read_text().splitlines()]
    check("rebase_spu_elf.py" in names and "spu_lifter.py" in names, "with flashMP3.pic the MP3 decoder is lifted")

    # vfs
    game = files / "NPUA80856"
    (game / "TROPDIR" / "NPWR00001_00").mkdir(parents=True)
    (game / "PARAM.SFO").write_text("sfo")
    (game / "TROPDIR" / "NPWR00001_00" / "TROPHY.TRP").write_text("trp")
    make_psarc(files / "data_ps3.psarc", {"/Movies/intro.avi": b"AVI1" * 100, "/Menu/menu.avi": b"AVI2",
                                         "/levels/l1.core": b"core"})
    shutil.copy(ROOT / "tools" / "psarc_ls.py", root / "tools" / "psarc_ls.py")
    env = dict(base, KZ_GAME=str(game), KZ_EBOOT=str(files / "EBOOT.elf"), KZ_PSARC=str(files / "data_ps3.psarc"))
    e = kzbuild.resolve(env, root=str(root))
    u = root / "work" / "vfs" / "PS3_GAME" / "USRDIR"
    so = sys.stdout
    sys.stdout = out = io.StringIO()
    try:
        kzbuild.cmd_vfs(e, None)
        (u / "save.dat").write_text("mine")
        kzbuild.cmd_vfs(e, None)
    finally:
        sys.stdout = so
    check("(2 movies)" in out.getvalue(), f"two movies: {out.getvalue()}")
    check((u / "Movies" / "intro.avi").read_bytes() == b"AVI1" * 100 and (u / "Menu" / "menu.avi").is_file()
          and not (u / "levels").exists(), "the movies, and only them, are taken out of the archive")
    check(os.path.samefile(u / "EBOOT.elf", files / "EBOOT.elf") and os.path.samefile(u / "data_ps3.psarc",
          files / "data_ps3.psarc"), "the EBOOT and the archive are linked")
    check((u.parent / "PARAM.SFO").read_text() == "sfo" and (u.parent / "TROPDIR" / "NPWR00001_00" /
          "TROPHY.TRP").is_file(), "PARAM.SFO and TROPDIR are copied")
    check((u / "save.dat").read_text() == "mine", "a second run keeps the saves")

    # build, with stand-ins for CMake and Ninja
    bin_ = tmp / "bin"
    bin_.mkdir()
    for t in ("cmake", "ninja"):
        (bin_ / t).write_text('#!/bin/sh\necho "$(basename "$0") $*" >> "$KZ_WORK/build.log"\n')
        (bin_ / t).chmod(0o755)
    e = kzbuild.resolve(dict(base, PATH=str(bin_) + os.pathsep + base.get("PATH", ""), KZ_JOBS="3"), root=str(root))
    try:
        kzbuild.cmd_build(e, type("A", (), {"win": False})())
        check(False, "build without a lift is refused")
    except kzbuild.Fail as err:
        check("run tools/lift.sh first" in str(err), str(err))
    (root / "work" / "lift" / "gen").mkdir(parents=True)
    (root / "work" / "lift" / "gen" / "ppu_recomp.h").write_text("")
    sys.stdout = io.StringIO()
    try:
        kzbuild.cmd_build(e, type("A", (), {"win": False})())
    finally:
        sys.stdout = so
    log = (root / "work" / "build.log").read_text().splitlines()
    check(log[0].startswith("cmake -G Ninja -S ") and "-DKZ_MARCH=x86-64-v3" in log[0] and
          f"-DRECOMP_DIR={root / 'work' / 'lift' / 'gen'}" in log[0], f"cmake configures the port: {log[0]}")
    check(log[1] == f"ninja -C {root / 'work' / 'build'} -j 3", f"ninja builds with KZ_JOBS: {log[1]}")

# the wrappers run kzbuild.py
for script, cmd in [("lift.sh", "lift"), ("setup-vfs.sh", "vfs"), ("build.sh", "build"),
                    ("ps3recomp-patched.sh", "patch"), ("build-win.sh", "build --win")]:
    text = (ROOT / "tools" / script).read_text()
    check(f'kzbuild.py" {cmd} "$@"' in text, f"tools/{script} runs kzbuild.py {cmd}")
r = subprocess.run([sys.executable, str(ROOT / "tools" / "kzbuild.py"), "env"], capture_output=True, text=True,
                   env=dict(base, KZ_EBOOT="/x/EBOOT.elf"))
check(r.returncode == 0 and "KZ_EBOOT=/x/EBOOT.elf" in r.stdout, f"kzbuild.py env: {r.stdout}{r.stderr}")

print("test_kzbuild:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
