#!/usr/bin/env python3
"""Host test for tools/release/assemble_payload.py and tools/release/launch.py: the payload of this repository (with
a stand-in toolchain cache) keeps the repository's layout, holds only the allowlist, the shipped patched ps3recomp and
the Windows toolchain entries with their own fetched.json (the Linux folder: run.sh, play.sh and the pinned Zig
toolchain with its other targets pruned, as a reproducible tar with execute bits), gives the embeddable Python's ._pth the tools folders, and
has a manifest without timestamps; the same sources give the same zip; the scrub refuses game-data names and magics,
home paths, a big file and a case clash; a toolchain entry not fetched from its pin or a stale patched tree refuses
the payload; launch.py names the missing step and starts the build from the run folder with play.sh's settings.
Needs work/ps3recomp-patched (tools/ps3recomp-patched.sh; CI runs it first)."""
import hashlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
sys.path.insert(0, str(ROOT / "tools"))
import assemble_payload as ap  # noqa: E402
import fetch_toolchain as ft  # noqa: E402
import launch  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def fake_cache(cache, m):
    """Each Windows entry as a small tree, fetched.json holding the manifest's hashes, and a Linux entry too."""
    done = {}
    for e in m["components"]:
        t = cache / e["name"]
        (t / "bin").mkdir(parents=True)
        (t / "LICENSE.txt").write_text(f"{e['name']} licence\n")
        (t / "bin" / f"{e['name']}.exe").write_bytes(b"MZ" + e["name"].encode())
        done[e["name"]] = e["sha256"]
    (cache / "python" / "python313._pth").write_text("python313.zip\n.\n\n# Uncomment to run site.main()\n#import site\n")
    linux = cache / "llvm-mingw" / "lib" / "clang" / "23" / "lib" / "linux"
    linux.mkdir(parents=True)
    (linux / "libclang_rt.asan-x86_64.so").write_bytes(b"\x7fELF")
    (cache / "zlib" / "amiga").mkdir()
    (cache / "zlib" / "amiga" / "Makefile.pup").write_text("all:\n")
    (cache / "downloads").mkdir()
    (cache / "downloads" / "big.zip").write_bytes(b"x")
    (cache / "fetched.json").write_text(json.dumps(done))
    return done


if not (ROOT / "work" / "ps3recomp-patched" / ".patchset").is_file():
    print("test_payload: no work/ps3recomp-patched: run tools/ps3recomp-patched.sh first")
    sys.exit(1)

m = ft.load()
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cache = tmp / "cache"
    fake_cache(cache, m)
    # the player's own DLSS download sits in the same cache: it is never collected
    (cache / "dlss-sdk" / "lib" / "Windows_x86_64" / "rel").mkdir(parents=True)
    (cache / "dlss-sdk" / "lib" / "Windows_x86_64" / "rel" / "nvngx_dlss.dll").write_bytes(b"MZnvidia")
    files, gen, man = ap.plan(str(ROOT), str(cache))
    paths = set(files) | set(gen)
    check(not any("dlss" in p.lower() and "rsx_vk_dlss" not in p or "nvngx" in p.lower() for p in paths),
          f"no NVIDIA file in the payload: {sorted(p for p in paths if 'nvngx' in p.lower() or 'dlss-sdk' in p)}")
    (cache / "sdl2" / "bin" / "nvngx_dlss.dll").write_bytes(b"MZnvidia")
    try:
        ap.plan(str(ROOT), str(cache))
        check(False, "an NVIDIA file in a toolchain entry is refused")
    except ap.Refused as err:
        check("nvngx_dlss.dll" in str(err) and "NVIDIA" in str(err), str(err))
    (cache / "sdl2" / "bin" / "nvngx_dlss.dll").unlink()
    for need in ("tools/kzbuild.py", "tools/release/firstrun.py", "tools/release/launch.py", "run.cmd", "play.cmd",
                 "port/kz1hd/CMakeLists.txt", "port/cmake/llvm-mingw.cmake", "re/function_seeds.txt", "VERSION",
                 "work/ps3recomp-patched/.patchset", "work/ps3recomp-patched/tools/ppu_lifter.py",
                 "work/toolchain-cache/fetched.json", "work/toolchain-cache/llvm-mingw/bin/llvm-mingw.exe",
                 "third_party/fidelityfx-fsr3/src/backends/vk/ffx_vk.cpp", "tools/ffx/ffx_mingw_compat.h"):
        check(need in paths, f"the payload has {need}")
    check(not any(p.startswith(("third_party/ps3recomp/", "port/patches/", ".github/", "work/toolchain-cache/downloads/",
                                "work/toolchain-cache/llvm-mingw-linux/", "work/ps3recomp-patched/docs/"))
                  for p in paths), "no submodule, patches, workflows, downloads, Linux compiler or ps3recomp docs")
    check(not any("/lib/linux/" in p or "/amiga/" in p for p in paths), "clang's Linux runtimes and zlib's Amiga "
          "files are left out")
    check(not any("__pycache__" in p or p.endswith(".pyc") for p in paths), "no Python caches")
    tc = {e["name"] for e in ft.entries_for(m, "windows")}
    check(json.loads(gen["work/toolchain-cache/fetched.json"]) == {n: e["sha256"] for e in m["components"]
                                                                   for n in [e["name"]] if n in tc},
          "fetched.json holds the Windows entries only")
    pth = gen["work/toolchain-cache/python/python313._pth"].decode()
    check(pth.split("\r\n")[:2] == ["python313.zip", "."] and "..\\..\\..\\tools" in pth and
          "..\\..\\ps3recomp-patched\\tools" in pth and "#" not in pth, f"the ._pth names the tools folders: {pth!r}")
    check(man["count"] == len(paths) and man["patchset"] and set(man) == {"format", "version", "platform", "commit",
                                                        "patchset", "toolchain", "count", "bytes", "files"}
          and man["platform"] == "windows"
          and {t["name"] for t in man["toolchain"]} == tc, "the manifest: every file, the patch set, the toolchain")

    out = tmp / "payload"
    r = ap.main(["--out", str(out), "--cache", str(cache), "--zip", str(tmp / "a.zip")])
    check(r == 0 and (out / "manifest.json").is_file() and (out / "run.cmd").read_bytes().endswith(b"\r\n"),
          "the payload is written, run.cmd with CRLF")
    check(ap.main(["--out", str(out), "--cache", str(cache)]) == 1, "an existing folder needs --replace")
    ap.main(["--out", str(out), "--cache", str(cache), "--replace", "--zip", str(tmp / "b.zip")])
    check((tmp / "a.zip").read_bytes() == (tmp / "b.zip").read_bytes(), "the same sources give the same zip")
    with zipfile.ZipFile(tmp / "a.zip") as z:
        check(all(n.startswith("project-vekta-") for n in z.namelist()), "the zip has one top folder")
    for p, e in [(p, e) for e in man["files"][:50] for p in [out / e["path"]]]:
        check(hashlib.sha256(p.read_bytes()).hexdigest() == e["sha256"], f"manifest hash of {e['path']}")

    # the Linux folder: the pinned Zig toolchain and the sources it builds, run.sh/play.sh, no Windows files
    lin = ft.entries_for(m, "native")
    for e in lin:
        t = cache / e["name"]
        t.mkdir(parents=True, exist_ok=True)
        (t / "LICENSE").write_text(f"{e['name']} licence\n")
        f = json.loads((cache / "fetched.json").read_text())
        f[e["name"]] = e["sha256"]
        (cache / "fetched.json").write_text(json.dumps(f))
    z = cache / "zig"
    for keep in ("lib/libc/include/x86-linux-gnu/bits/a.h", "lib/libc/include/generic-glibc/b.h",
                 "lib/libc/glibc/abilists", "lib/std/std.zig"):
        (z / keep).parent.mkdir(parents=True, exist_ok=True)
        (z / keep).write_text("x\n")
    for drop in ("lib/libc/include/aarch64-linux-gnu/c.h", "lib/libc/musl/d.c", "lib/libc/mingw/e.c", "doc/x.html",
                 "RECORD"):
        (z / drop).parent.mkdir(parents=True, exist_ok=True)
        (z / drop).write_text("x\n")
    (z / "zig").write_bytes(b"\x7fELF" + b"\0" * 60)
    (z / "zig").chmod(0o755)
    (cache / "ffmpeg-linux" / "lib").mkdir(parents=True, exist_ok=True)
    (cache / "ffmpeg-linux" / "lib" / "libavutil.so.61.1.100").write_bytes(b"\x7fELF")
    (cache / "ffmpeg-linux" / "lib" / "libavutil.so").write_bytes(b"\x7fELF")
    lf, lg, lman = ap.plan(str(ROOT), str(cache), platform="linux")
    lp = set(lf) | set(lg)
    for need in ("run.sh", "play.sh", "tools/release/zig/cc", "tools/release/build_linux_deps.py", "tools/play.sh",
                 "port/cmake/zig-linux.cmake", "work/toolchain-cache/zig/zig",
                 "work/toolchain-cache/zig/lib/libc/include/x86-linux-gnu/bits/a.h",
                 "work/toolchain-cache/zig/lib/libc/include/generic-glibc/b.h",
                 "work/toolchain-cache/zig/lib/std/std.zig",
                 "work/toolchain-cache/ffmpeg-linux/lib/libavutil.so.61.1.100"):
        check(need in lp, f"the Linux folder has {need}")
    check(not any(p in lp for p in ("run.cmd", "play.cmd")) and not any(p.endswith("._pth") for p in lp),
          "the Linux folder has no .cmd files and no ._pth")
    check(not any(p.startswith(("work/toolchain-cache/llvm-mingw/", "work/toolchain-cache/python/"))
                  for p in lp), "the Linux folder has no Windows toolchain")
    check(not any(s in p for p in lp for s in ("aarch64-linux-gnu", "/musl/", "/mingw/", "zig/doc/", "zig/RECORD"))
          and "work/toolchain-cache/ffmpeg-linux/lib/libavutil.so" not in lp,
          "Zig's other targets, docs and wheel files and FFmpeg's link names are left out")
    ltc = {e["name"] for e in ap.linux_entries(m)}
    check(lman["platform"] == "linux" and {t["name"] for t in lman["toolchain"]} == ltc and
          set(json.loads(lg["work/toolchain-cache/fetched.json"])) == ltc and
          {"zig", "zlib", "glslang", "vulkan-headers"} <= ltc and "llvm-mingw" not in ltc,
          f"the Linux manifest and fetched.json hold the native entries and their sources: {sorted(ltc)}")
    check(ap.main(["--out", str(tmp / "lx"), "--cache", str(cache), "--platform", "linux", "--zip",
                   str(tmp / "l.zip")]) == 1, "the Linux folder refuses --zip")
    r = ap.main(["--out", str(tmp / "lx"), "--cache", str(cache), "--platform", "linux", "--tar", str(tmp / "new" / "a.tgz")])
    ap.main(["--out", str(tmp / "lx"), "--cache", str(cache), "--platform", "linux", "--replace", "--tar",
             str(tmp / "b.tgz")])
    check(r == 0 and (tmp / "new" / "a.tgz").read_bytes() == (tmp / "b.tgz").read_bytes(),
          "the same sources give the same tar (its folder is made)")
    import tarfile
    with tarfile.open(tmp / "new" / "a.tgz") as t:
        mem = {i.name: i for i in t.getmembers()}
    v = lman["version"]
    top = f"project-vekta-{v}-linux/"
    check(all(n.startswith(top) for n in mem), "the tar has one top folder")
    for x in ("run.sh", "play.sh", "tools/release/zig/cc", "work/toolchain-cache/zig/zig"):
        check(mem.get(top + x) is not None and mem[top + x].mode == 0o755, f"{x} keeps its execute bit in the tar")
    check(mem[top + "VERSION"].mode == 0o644 and mem[top + "VERSION"].uid == 0, "a plain file is 644, owner 0")
    check(ap.scrub("t/zig", str(z / "zig"), toolchain=True, elf_ok=True) is None and
          ap.scrub("t/zig", str(z / "zig"), toolchain=True), "ELF passes only where the Linux toolchain allows it")

    # refusals
    bad = tmp / "bad"
    bad.mkdir()
    for name, data, why in [("EBOOT.BIN", b"x", "game-data name"), ("x.psarc", b"x", "game-data name"),
                            ("a.bin", b"\x7fELF....", "ELF"), ("s.sfo", b"\0PSF....", "game"),
                            ("n.txt", b"see /ho" b"me/jane/files", "home path"),
                            ("w.txt", b"C:\\Us" b"ers\\jane\\x", "home path"),
                            ("big.c", b"x" * (ap.MAX_FILE + 1), "MB")]:
        (bad / name).write_bytes(data)
        got = ap.scrub(name, str(bad / name))
        check(got and why in got, f"{name} is refused ({why}): {got}")
    (bad / "ok.txt").write_text("C:\\Us" "ers\\Public\\x and %USERPROFILE%")
    check(ap.scrub("ok.txt", str(bad / "ok.txt")) is None, "Public and variables are not home paths")
    (bad / "t.exe").write_bytes(b"MZ" + b"/ho" b"me/builder/" + b"x" * 100)
    check(ap.scrub("t.exe", str(bad / "t.exe"), toolchain=True) is None, "a toolchain file gets the name check only")
    check(ap.case_clashes(["a/B.txt", "a/b.txt", "c"]) == ["a/B.txt / a/b.txt"], "a case clash is found")
    f = json.loads((cache / "fetched.json").read_text())
    f["cmake"] = "0" * 64
    (cache / "fetched.json").write_text(json.dumps(f))
    try:
        ap.plan(str(ROOT), str(cache))
        check(False, "a toolchain entry fetched from another hash is refused")
    except ap.Refused as err:
        check("cmake: not fetched from its pin" in str(err), str(err))

    # launch.py
    root = tmp / "tree"
    w = root / "work"
    env = {k: v for k, v in os.environ.items() if not k.startswith(("KZ_", "PS3"))}
    try:
        launch.plan(str(root), env)
        check(False, "launch without a run folder is refused")
    except ValueError as err:
        check("run tools/install.sh first" in str(err), str(err))
    u = w / "vfs" / "PS3_GAME" / "USRDIR"
    u.mkdir(parents=True)
    (u / "EBOOT.elf").write_text("e")
    try:
        launch.plan(str(root), env)
        check(False, "launch without a build is refused")
    except ValueError as err:
        check("no build at" in str(err), str(err))
    (w / "build").mkdir()
    exe = w / "build" / "KZ1HD"
    exe.write_text('#!/bin/sh\necho "cwd=$PWD arg=$1 skip=$GCM_PARK_SKIP flips=$PS3_GCM_MAX_QUEUED_FLIPS '
                   'img=$KZ_LIBSRE_IMAGE disp=$PS3RECOMP_VK_DISPLAY"\n')
    exe.chmod(0o755)
    argv, cwd, e, log = launch.plan(str(root), dict(env, PS3_GCM_MAX_QUEUED_FLIPS="2"), fullscreen=True)
    check(argv == [str(exe), str(u / "EBOOT.elf")] and cwd == str(w / "vfs"), f"the build runs from vfs: {argv}")
    check(launch.run(argv, cwd, e, log, poll=0.05) == 0, "the game's exit code comes back")
    check("PS3_VFS_ROOT" not in e, "from the run folder the game needs no PS3_VFS_ROOT")
    (root / launch.INSTALL_INI).write_text("[install]\nportable=0\n")
    _, cwd2, e2, _ = launch.plan(str(root), dict(env, APPDATA=str(tmp / "appdata")))
    check(cwd2 == str(tmp / "appdata" / "Project Vekta") and e2.get("PS3_VFS_ROOT") == str(w / "vfs").replace("\\", "/"),
          f"from the saves folder the game is told where its files are: {cwd2} {e2.get('PS3_VFS_ROOT')}")
    (root / launch.INSTALL_INI).unlink()
    text = pathlib.Path(log).read_text()
    check(f"cwd={w / 'vfs'}" in text and "skip=1 flips=2" in text and "disp=borderless" in text and
          f"img={w / 'fw' / 'out' / 'libsre_image.bin'}" in text, f"play.sh's settings, the environment wins: {text}")
    exe.write_text('#!/bin/sh\necho "window closed"\nsleep 30\n')
    import time
    t0 = time.time()
    launch.run(argv, cwd, e, log, poll=0.05)
    check(time.time() - t0 < 10, "a closed window stops the game")
    so = sys.stdout
    sys.stdout = io.StringIO()
    try:
        rc = launch.main(["--root", str(root), "--dry-run"])
        dry = sys.stdout.getvalue()
    finally:
        sys.stdout = so
    check(rc == 0 and "GCM_PARK_SKIP=1" in dry and "run:" in dry, f"--dry-run prints the plan: {dry}")

    # an install over an earlier release: the build its files did not make is rebuilt before the game starts
    import firstrun
    check(launch.stale_build(str(root)) is None, "a tree without the shipped patched ps3recomp is never stale")
    (w / "ps3recomp-patched").mkdir()
    (w / "ps3recomp-patched" / ".patchset").write_text("new\n")
    check("no stamp" in (launch.stale_build(str(root)) or ""), "a build without a stamp is stale")
    version = firstrun.read_version(str(root))
    stamp = {"version": version, "patchset": "old", "dlss": False}
    (w / "build-stamp.json").write_text(json.dumps(stamp))
    check("patchset old" in (launch.stale_build(str(root)) or ""), "a build of another patch set is stale")
    (w / "build-stamp.json").write_text(json.dumps(dict(stamp, patchset="new")))
    check(launch.stale_build(str(root)) is None, "the build of this install is not stale")
    firstrun.write_dlss(str(w), "accept")
    check("DLSS" in (launch.stale_build(str(root)) or ""), "a build without DLSS after DLSS was accepted is stale")
    (w / "build-stamp.json").write_text(json.dumps(dict(stamp, version="0.0.1", patchset="new", dlss=True)))
    check("version 0.0.1" in (launch.stale_build(str(root)) or ""), "a build of another version is stale")
    ran, said = [], []
    real_notify, real_first = launch.notify, launch.first_run
    launch.notify = lambda text, error=True: said.append(text)
    launch.first_run = lambda r: ran.append("run.cmd") or 0

    def fake_firstrun(argv, **kw):
        ran.append(argv[1:])
        st = argv[argv.index("--status-file") + 1]
        pathlib.Path(st).write_text("RESULT ok=0 step=compile\nREASON the compile broke\n")
        return subprocess.CompletedProcess(argv, 1)
    try:
        check(launch.update_build(str(root), run=fake_firstrun) and ran == ["run.cmd"],
              f"without saved paths the stale build is rebuilt by run.cmd, which asks for them: {ran}")
        (w / "paths.env").write_text("")
        ran.clear()
        check(not launch.update_build(str(root), run=fake_firstrun) and "--bar" in ran[0] and
              "the compile broke" in said[-1], f"a failed rebuild says why and starts no game: {ran} {said}")
        exe.write_text('#!/bin/sh\necho "ERROR: could not load the EBOOT"\nexit 1\n')
        argv, cwd, e, log = launch.plan(str(root), env)
        t0 = time.monotonic()
        rc = launch.run(argv, cwd, e, log, poll=0.05)
        text = launch.early_exit_text(rc, log)
        check(time.monotonic() - t0 < launch.EARLY_EXIT and "exit code 1" in text and "could not load the EBOOT" in
              text, f"a game that stops at once is reported with the end of its log: {text}")
    finally:
        launch.notify, launch.first_run = real_notify, real_first

r = subprocess.run([sys.executable, str(ROOT / "tools" / "release" / "assemble_payload.py"), "--dry-run", "--cache",
                    "/nonexistent"], capture_output=True, text=True)
check(r.returncode == 1 and "not fetched from its pin" in r.stderr, f"an empty cache is refused: {r.stderr}")

print("test_payload:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
