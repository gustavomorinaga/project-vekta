#!/usr/bin/env python3
"""Host test for tools/kzbuild.py, the build steps tools/ps3recomp-patched.sh, tools/lift.sh, tools/setup-vfs.sh and
tools/build.sh run (and the installer runs on Windows): the paths come from the environment, else work/paths.env,
else the defaults, as in tools/env.sh; patch applies the series to a copy, rewrites only the files that changed and
writes .patchset, and refuses a patch left out of the series; lift calls ps3recomp's lifters (stand-ins here) with
the arguments and native-function lists of the shell script it replaced, writes seeds.json and removes the work/fw
of earlier versions (the firmware libsre's lift); vfs copies PARAM.SFO and TROPDIR, links the EBOOT and the archive, takes the movies out of a small
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
    check("PS3_DEV_FLASH" not in kzbuild.resolve(base, root=str(root)), "no dev_flash path: no firmware is read")

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
              "build_spu_workloads", "spu_lifter"):
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
    files.mkdir(parents=True)
    (files / "EBOOT.elf").write_text("EBOOT.elf")
    env = dict(base, KZ_EBOOT=str(files / "EBOOT.elf"), KZ_JOBS="5")
    e = kzbuild.resolve(env, root=str(root))
    try:
        kzbuild.cmd_lift(e, None)
    except kzbuild.Fail as err:
        check(False, f"lift runs: {err}")
    calls = [json.loads(line) for line in (root / "work" / "argv.log").read_text().splitlines()]
    check([c[0] for c in calls] == ["ppu_loader.py", "find_functions.py", "ppu_lifter.py", "ppu_regcache.py",
                                    "extract_spu_images.py", "extract_spu_modules.py", "lift_jobmods.py",
                                    "build_spu_workloads.py"],
          f"the lifters run in lift.sh's order (no MP3 decoder lift any more): {[c[0] for c in calls]}")
    ppu = calls[2]
    check(ppu[2] == "func_0012,func_00AB" and ppu[3] is None, f"PPU_LIFT_NATIVE in file order, then line: {ppu[2:]}")
    check(ppu[1][ppu[1].index("-j") + 1] == "5" and "--setjmp" in ppu[1], f"the PPU lift's arguments: {ppu[1]}")
    check(calls[5][3] == "ab05_spu_func_01F0" and calls[5][2] == "func_0012,func_00AB", "SPU_LIFT_NATIVE from SPU on")
    check(json.loads((root / "work" / "lift" / "analysis" / "seeds.json").read_text()) == ["0x10", "0x20"],
          "seeds.json: the seed lines without comments or blanks")
    eid = (root / "work" / "lift" / "gen" / "eboot_id.h").read_text()
    data = pathlib.Path(e["KZ_EBOOT"]).read_bytes()
    check(f'#define KZ_EBOOT_SHA256 "{hashlib.sha256(data).hexdigest()}"' in eid and
          f"#define KZ_EBOOT_SIZE {len(data)}LL" in eid,
          f"the lift writes gen/eboot_id.h with the size and SHA-256 of the EBOOT it lifted (eboot_check.h): {eid}")
    (root / "work" / "argv.log").unlink()
    for d in ("flashmp3", "flashmp3_src"):   # an earlier version's lift of the firmware's MP3 decoder
        (root / "work" / "lift" / "spu" / d).mkdir(parents=True, exist_ok=True)
    # chunks an earlier lift left (another version's chunk count): the build globs these folders, so they go
    for d in ("lift", "fw"):
        (root / "work" / d / "gen").mkdir(parents=True, exist_ok=True)
        (root / "work" / d / "gen" / "old_recomp_099.cpp").write_text("int stale;\n")
    kzbuild.cmd_lift(e, None)
    names = [json.loads(line)[0] for line in (root / "work" / "argv.log").read_text().splitlines()]
    check("rebase_spu_elf.py" not in names and not any((root / "work" / "lift" / "spu" / d).exists()
          for d in ("flashmp3", "flashmp3_src")), "no MP3 decoder lift, and an earlier one's folders are removed")
    check(not (root / "work" / "lift" / "gen" / "old_recomp_099.cpp").exists(),
          "a lift leaves no source of an earlier lift in work/lift/gen")
    check(not (root / "work" / "fw").exists(), "a lift removes the firmware libsre's work/fw of earlier versions")

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

    # vfs and data with only the installed game's data_ps3.psarc.edat (patch 4014: the game opens it itself)
    (game / "USRDIR").mkdir()
    (game / "USRDIR" / "data_ps3.psarc.edat").write_bytes(b"NPD\0" + b"x" * 60)
    env_edat = dict(env, KZ_PSARC=str(files / "none.psarc"))
    e = kzbuild.resolve(env_edat, root=str(root))
    sys.stdout = out = io.StringIO()
    try:
        kzbuild.cmd_vfs(e, None)
    finally:
        sys.stdout = so
    check(os.path.samefile(u / "data_ps3.psarc.edat", game / "USRDIR" / "data_ps3.psarc.edat")
          and not os.path.lexists(u / "data_ps3.psarc"), "edat: the .edat is linked, the old archive link removed")
    check("kzbuild.py data" in out.getvalue(), f"edat: vfs says the movies come with the data step: {out.getvalue()}")
    shutil.rmtree(u / "Movies")
    shutil.rmtree(u / "Menu")
    fake = tmp / "fake-kz1hd"
    plain = files / "data_ps3.psarc"
    fake.write_text("#!/bin/sh\n"
                    'echo "$KZ_PREPARE_DATA $PS3RECOMP_VK_WINDOW $PWD" >> "$KZ_WORK/fake.log"\n'
                    'mkdir -p "$KZ_WORK/edat-cache" && cp "$FAKE_PLAIN" "$KZ_WORK/edat-cache/0123456789abcdef.dec"\n'
                    'echo "[kz] data ready: $KZ_WORK/edat-cache/0123456789abcdef.dec"\n')
    fake.chmod(0o755)
    e = kzbuild.resolve(dict(env_edat, KZ_BIN=str(fake), FAKE_PLAIN=str(plain)), root=str(root))
    size0, sha0 = kzbuild.PSARC_SIZE, kzbuild.PSARC_SHA256
    kzbuild.PSARC_SIZE, kzbuild.PSARC_SHA256 = plain.stat().st_size, kzbuild.sha256_file(plain)
    sys.stdout = out = io.StringIO()
    try:
        kzbuild.cmd_data(e, None)
        kzbuild.cmd_data(e, None)
    finally:
        sys.stdout = so
    calls = (root / "work" / "fake.log").read_text().splitlines()
    check(calls == [f"1 0 {root / 'work' / 'vfs'}"], f"data: one headless run from the run folder: {calls}")
    check((u / "Movies" / "intro.avi").is_file() and "already prepared" in out.getvalue(),
          f"data: the movies come out of the opened archive, and a second run does nothing: {out.getvalue()}")
    (root / "work" / "edat-cache" / "data_ps3.psarc.ok").unlink()
    kzbuild.PSARC_SHA256 = "0" * 64
    sys.stdout = io.StringIO()
    try:
        kzbuild.cmd_data(e, None)
        check(False, "data: an archive that is not the tested one is refused")
    except kzbuild.Fail as err:
        check("not the tested one" in str(err), str(err))
    finally:
        sys.stdout = so
    kzbuild.PSARC_SIZE, kzbuild.PSARC_SHA256 = size0, sha0
    e = kzbuild.resolve(env, root=str(root))
    sys.stdout = io.StringIO()
    try:
        kzbuild.cmd_vfs(e, None)
    finally:
        sys.stdout = so
    check(os.path.lexists(u / "data_ps3.psarc") and not os.path.lexists(u / "data_ps3.psarc.edat"),
          "a decrypted data_ps3.psarc wins over the .edat (installs up to 0.4.0)")

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
    (root / "work" / "lift" / "gen").mkdir(parents=True, exist_ok=True)   # the lift above left eboot_id.h there
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
