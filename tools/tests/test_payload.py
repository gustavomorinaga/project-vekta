#!/usr/bin/env python3
"""Host test for tools/release/assemble_payload.py and tools/release/launch.py: the payload of this repository (with
a stand-in toolchain cache) keeps the repository's layout, holds only the allowlist, the shipped patched ps3recomp and
the Windows toolchain entries with their own fetched.json (the Linux folder: project-vekta, scripts/ and the pinned Zig
toolchain with its other targets pruned, as a reproducible tar with execute bits), gives the embeddable Python's ._pth the tools folders, and
has a manifest without timestamps; the same sources give the same zip; the scrub refuses game-data names and magics,
home paths, a big file and a case clash; a toolchain entry not fetched from its pin or a stale patched tree refuses
the payload; launch.py names the missing step and starts the build from the run folder with play.sh's settings.
Needs work/ps3recomp-patched (tools/ps3recomp-patched.sh; CI runs it first)."""
import hashlib
import io
import json
import re
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


TC = "work/toolchain-cache/"
UNUSED_KEPT = {TC + "glslang/glslang/CMakeLists.txt", TC + "vulkan-headers/include/vulkan/vulkan.h"}
UNUSED_DROPPED = {TC + "glslang/Test/baseResults/100.frag.out", TC + "vulkan-headers/registry/vk.xml"}


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
    # what neither build reads, beside what both read: glslang's CMake files and sources, the Vulkan headers
    for keep, drop in (("glslang/glslang/CMakeLists.txt", "glslang/Test/baseResults/100.frag.out"),
                       ("vulkan-headers/include/vulkan/vulkan.h", "vulkan-headers/registry/vk.xml")):
        for f in (keep, drop):
            (cache / f).parent.mkdir(parents=True, exist_ok=True)
            (cache / f).write_text("x\n")
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
    for need in ("tools/kzbuild.py", "tools/release/firstrun.py", "tools/release/launch.py", "scripts/run.cmd", "scripts/play.cmd",
                 "scripts/prepare.cmd", "scripts/support.cmd", "tools/release/sessions.py", "tools/release/symbols.py",
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
    check(UNUSED_KEPT <= paths and not UNUSED_DROPPED & paths, "glslang's test shaders and the Vulkan registry are "
          f"left out, the sources and headers stay: {sorted((UNUSED_KEPT - paths) | (UNUSED_DROPPED & paths))}")
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
    check(r == 0 and (out / "manifest.json").is_file() and (out / "scripts" / "run.cmd").read_bytes().endswith(b"\r\n"),
          "the payload is written, scripts/run.cmd with CRLF")
    top = sorted(rel for rel in set(files) | set(gen) if "/" not in rel)
    check(not [r for r in top if ap.startable(r, files.get(r, ""), "windows")],
          f"without --launcher the Windows top has no startable file at all: {top}")
    launcher = tmp / "Project Vekta.exe"
    launcher.write_bytes(b"MZ")
    wf, _, _ = ap.plan(str(ROOT), str(cache), str(launcher))
    rl = json.loads(gen["recomplauncher.json"])
    wmf = {e["path"] for e in ap.plan(str(ROOT), str(cache), str(launcher))[2]["files"]}
    check("recomplauncher.json" in wmf and any(p in wmf for p in rl["start"]["program"]) and
          any(p in wmf for p in rl["prepare"]["program"]),
          "recomplauncher.json is in the Windows manifest, and its start and prepare programs are in the folder")
    check([r for r in wf if "/" not in r and ap.startable(r, wf[r], "windows")] == ["Project Vekta.exe"],
          "with it, Project Vekta.exe is the one startable file at the Windows top (Quiver's rule)")
    check(ap.layout_problems(dict(wf, **{"run.cmd": str(launcher)}), "windows") and
          ap.layout_problems({"VERSION": str(launcher)}, "windows"),
          "a second .cmd at the top, or no launcher at all, is refused")
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
    for need in ("project-vekta", "scripts/run.sh", "scripts/play.sh", "scripts/prepare.sh", "scripts/support.sh",
                 "tools/release/sessions.py", "tools/release/symbols.py", "tools/release/zig/cc", "tools/release/build_linux_deps.py", "tools/play.sh",
                 "port/cmake/zig-linux.cmake", "work/toolchain-cache/zig/zig",
                 "work/toolchain-cache/zig/lib/libc/include/x86-linux-gnu/bits/a.h",
                 "work/toolchain-cache/zig/lib/libc/include/generic-glibc/b.h",
                 "work/toolchain-cache/zig/lib/std/std.zig",
                 "work/toolchain-cache/ffmpeg-linux/lib/libavutil.so.61.1.100"):
        check(need in lp, f"the Linux folder has {need}")
    check(not any(p.endswith(".cmd") for p in lp) and not any(p.endswith("._pth") for p in lp),
          "the Linux folder has no .cmd files and no ._pth")
    lrl = json.loads(lg["recomplauncher.json"])
    lmf = {e["path"] for e in lman["files"]}
    check("recomplauncher.json" in lmf and any(p in lmf for p in lrl["start"]["program"]) and
          any(p in lmf for p in lrl["prepare"]["program"]),
          "recomplauncher.json is in the Linux manifest, and its start and prepare programs are in the folder")
    check([r for r in lf if "/" not in r and ap.startable(r, lf[r], "linux")] == ["project-vekta"],
          "project-vekta is the one startable file at the Linux top (.sh, #!/ or ELF: Quiver's rule)")
    elf = tmp / "elf"
    elf.write_bytes(b"\x7fELF....")
    shebang = tmp / "noext"
    shebang.write_text("#!/bin/sh\n")
    check(ap.startable("x.sh", str(tmp / "VERSIONX"), "linux") and ap.startable("tool", str(elf), "linux") and
          ap.startable("noext", str(shebang), "linux") and not ap.startable("lib.so", str(elf), "linux") and
          not ap.startable("notes.txt", str(shebang), "linux"),
          "on Linux .sh, #!/ and ELF files count; .so and .txt never")
    check(not any(p.startswith(("work/toolchain-cache/llvm-mingw/", "work/toolchain-cache/python/"))
                  for p in lp), "the Linux folder has no Windows toolchain")
    check(UNUSED_KEPT <= lp and not UNUSED_DROPPED & lp, "the Linux folder too leaves out glslang's test shaders and "
          f"the Vulkan registry: {sorted((UNUSED_KEPT - lp) | (UNUSED_DROPPED & lp))}")
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
    check(ap.main(["--out", str(tmp / "lx"), "--cache", str(cache), "--platform", "linux", "--tar",
                   str(tmp / "a.tar.zst")]) == 1 and not (tmp / "lx").exists(),
          "a --tar name that is neither .tar.xz nor .tar.gz is refused before anything is written")
    r = ap.main(["--out", str(tmp / "lx"), "--cache", str(cache), "--platform", "linux", "--tar", str(tmp / "new" / "a.tar.xz")])
    ap.main(["--out", str(tmp / "lx"), "--cache", str(cache), "--platform", "linux", "--replace", "--tar",
             str(tmp / "b.tar.xz")])
    check(r == 0 and (tmp / "new" / "a.tar.xz").read_bytes() == (tmp / "b.tar.xz").read_bytes(),
          "the same sources give the same tar (its folder is made)")
    import tarfile
    with tarfile.open(tmp / "new" / "a.tar.xz", "r:xz") as t:
        mem = {i.name: i for i in t.getmembers()}
    # the .tar.gz of the releases up to 0.6.0 is still written when the name asks for it, with the same members
    ap.main(["--out", str(tmp / "lx"), "--cache", str(cache), "--platform", "linux", "--replace", "--tar",
             str(tmp / "c.tgz")])
    with tarfile.open(tmp / "c.tgz", "r:gz") as t:
        gz_names = [i.name for i in t.getmembers()]
    check(gz_names == list(mem) and (tmp / "new" / "a.tar.xz").read_bytes()[:6] == b"\xfd7zXZ\x00",
          "a .tgz name packs the same members with gzip; the .tar.xz is an xz file")
    v = lman["version"]
    top = f"project-vekta-{v}-linux/"
    check(all(n.startswith(top) for n in mem), "the tar has one top folder")
    for x in ("project-vekta", "scripts/run.sh", "scripts/play.sh", "scripts/prepare.sh", "scripts/support.sh",
              "tools/release/zig/cc",
              "work/toolchain-cache/zig/zig"):
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
          "img= " in text, f"play.sh's settings, the environment wins, no firmware image: {text}")
    exe.write_text('#!/bin/sh\necho "window closed"\nsleep 30\n')
    import time
    t0 = time.time()
    launch.run(argv, cwd, e, log, poll=0.05)
    check(time.time() - t0 < 10, "a closed window stops the game")
    # while the game runs, the watcher reads only what the log gained (it read the whole log every second before)
    grow = tmp / "growing.log"
    seen = [0, b""]
    check(not launch.closed_since(str(grow), seen) and seen == [0, b""], "no log yet: not closed")
    grow.write_bytes(b"[rsx] frame\n" * 4000)
    reads = []
    real_open = open

    class Counted:
        """The log's file, noting how many bytes each read returns."""
        def __init__(self, f):
            self.f, self.fileno, self.seek = f, f.fileno, f.seek

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            self.f.close()

        def read(self):
            data = self.f.read()
            reads.append(len(data))
            return data

    def counting_open(path, *a, **k):
        f = real_open(path, *a, **k)
        return Counted(f) if str(path) == str(grow) else f
    launch.open = counting_open
    try:
        check(not launch.closed_since(str(grow), seen) and reads == [48000] and seen[0] == 48000,
              f"the first look reads what is there: {reads} {seen[0]}")
        check(not launch.closed_since(str(grow), seen) and reads[-1] == 0, f"nothing new: nothing read: {reads}")
        with real_open(grow, "ab") as f:
            f.write(b"[rsx] frame\nwindow clo")
        check(not launch.closed_since(str(grow), seen) and reads[-1] == 22, f"only the new bytes are read: {reads}")
        with real_open(grow, "ab") as f:
            f.write(b"sed\n")
        check(launch.closed_since(str(grow), seen) and reads[-1] == 4,
              f"a mark that lies across two reads is seen: {reads}")
        # the exe empties its log when it starts: a log shorter than what was read is read from its start
        grow.write_bytes(b"window disabled\n")
        check(launch.closed_since(str(grow), seen) and reads[-1] == 16 and seen[0] == 16,
              f"an emptied log is read from its start: {reads}")
    finally:
        del launch.open
    # On Windows the exe writes its own log (KZ_LOG), and only once it runs: the last session's "window closed" in
    # that file must not stop the next one before it has started (an install over an earlier version keeps the log)
    pathlib.Path(log).write_text("[kz] quitting (window closed)\n")
    exe.write_text('#!/bin/sh\nsleep 0.5\necho "started" > "$KZ_LOG"\nexit 0\n')
    rc = launch.run(argv, cwd, dict(e, KZ_LOG=log), log, poll=0.05)
    check(rc == 0 and pathlib.Path(log).read_text() == "started\n",
          f"the last session's log does not stop the game that writes its own: {rc} {pathlib.Path(log).read_text()!r}")
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
        exe.write_text('#!/bin/sh\necho "[kz] quitting (Quit Game)"\nexit 0\n')
        argv, cwd, e, log = launch.plan(str(root), env)
        rc = launch.run(argv, cwd, e, log, poll=0.05)
        check(rc == 0 and launch.window_closed(log),
              "Quit Game seconds after the start is the player's choice, not a failed start (2026-11-19 report)")
        exe.write_text('#!/bin/sh\necho "ERROR: could not load the EBOOT"\nexit 1\n')
        argv, cwd, e, log = launch.plan(str(root), env)
        t0 = time.monotonic()
        rc = launch.run(argv, cwd, e, log, poll=0.05)
        text = launch.early_exit_text(rc, log)
        check(time.monotonic() - t0 < launch.EARLY_EXIT and "exit code 1" in text and "could not load the EBOOT" in
              text, f"a game that stops at once is reported with the end of its log: {text}")
        # KZ1HD's own exit codes (port/kz1hd/kz_exit.h): the CPU check's code gets the exe's own line from the log,
        # else the stock text; any other code is not one of them
        line = ("This build of Project Vekta needs a CPU with x86-64-v3 (AVX2), and this one has x86-64-v2. The game "
                "cannot run on it.")
        exe.write_text(f'#!/bin/sh\necho "[kz] CPU check: {line}"\nexit {launch.EXIT_CPU}\n')
        argv, cwd, e, log = launch.plan(str(root), env)
        rc = launch.run(argv, cwd, e, log, poll=0.05)
        check(rc == launch.EXIT_CPU and launch.exit_code_text(rc, log) == line,
              f"the CPU check's exit code shows the exe's own line: {rc} {launch.exit_code_text(rc, log)}")
        pathlib.Path(log).write_text("")
        check(launch.exit_code_text(launch.EXIT_CPU, log) == launch.EXIT_TEXT[launch.EXIT_CPU] and
              "x86-64-v3" in launch.EXIT_TEXT[launch.EXIT_CPU],
              "and kz_exit.h's text when the log has no line")
        check(launch.exit_code_text(1, log) is None and launch.exit_code_text(0, log) is None,
              "other exit codes are not the exe's own")
        # No Vulkan at start (#131): KZ1HD exits with NO_VULKAN and its own line, which the launcher shows
        vk_line = ("The game could not start Vulkan on this PC (see the lines above). Install or update your "
                   "graphics card's driver.")
        pathlib.Path(log).write_text("vkCreateInstance failed (VkResult -9)\n[rsx] Vulkan backend init FAILED\n"
                                     f"[kz] Vulkan check: {vk_line}\n")
        check(launch.exit_code_text(launch.NO_VULKAN, log) == vk_line,
              f"the Vulkan check's code shows the exe's own line: {launch.exit_code_text(launch.NO_VULKAN, log)}")
        main_cpp = (ROOT / "port" / "kz1hd" / "main.cpp").read_text()
        check(f'"[kz] Vulkan check: {vk_line[:40]}' in main_cpp and "_exit(KZ_EXIT_NO_VULKAN)" in main_cpp,
              "KZ1HD writes that line and exits with KZ_EXIT_NO_VULKAN when Vulkan does not start")
        # The EBOOT check's code: the exe's own line, then the rebuild a stale build gets, and the game starts again
        eboot_line = ("This build was made from another EBOOT.elf than the one in /x/USRDIR. Run 'Verify and rebuild' "
                      "(Windows) or ./project-vekta --prepare (Linux).")
        pathlib.Path(log).write_text("[kz] EBOOT check: built from 0123, this one is 4567\n"
                                     f"[kz] EBOOT check: {eboot_line}\n")
        check(launch.exit_code_text(launch.EXIT_EBOOT, log) == eboot_line,
              f"the EBOOT check's code shows the exe's own line: {launch.exit_code_text(launch.EXIT_EBOOT, log)}")
        real = {k: getattr(launch, k) for k in ("plan", "run", "ask", "update_build", "stale_build")}
        runs, asked, rebuilt = [], [], []
        planned = launch.plan(str(root), env)
        launch.plan = lambda root, fullscreen=False: planned

        def eboot_run(argv, cwd, e, log, **kw):
            # the game writes its log while it runs: the last one has moved to work/logs (sessions.rotate)
            runs.append(1)
            if len(runs) == 1:
                pathlib.Path(log).write_text("[kz] EBOOT check: built from 0123, this one is 4567\n"
                                             f"[kz] EBOOT check: {eboot_line}\n")
                return launch.EXIT_EBOOT
            pathlib.Path(log).write_text("[kz] quitting (window closed)\n")
            return 0
        launch.run = eboot_run
        launch.stale_build = lambda root: None
        launch.update_build = lambda root, run=None: rebuilt.append(root) or True
        try:
            launch.ask = lambda text: asked.append(text) or True
            rc = launch.main(["--root", str(root), "--build-if-needed"])
            check(rc == 0 and len(runs) == 2 and rebuilt and asked and eboot_line in asked[0] and "Rebuild" in asked[0],
                  f"--build-if-needed: the EBOOT code offers the rebuild, then starts the game again: rc {rc}, "
                  f"{len(runs)} runs, {asked}")
            runs.clear(); rebuilt.clear()
            launch.ask = lambda text: False
            rc = launch.main(["--root", str(root), "--build-if-needed"])
            check(rc == launch.EXIT_EBOOT and len(runs) == 1 and not rebuilt, "no: nothing is rebuilt, the code stays")
        finally:
            for k, v in real.items():
                setattr(launch, k, v)
    finally:
        launch.notify, launch.first_run = real_notify, real_first

# Every file or folder above port/kz1hd the player's build reads ("../../<path>" in CMakeLists.txt, app.rc and the
# CMake scripts beside them) is shipped in both folders: the build runs from the payload, not from a Git clone. A
# path the rules miss broke the 0.6.0 test build on Windows (third_party/minimp3, 2026-11-20).
refs = set()
for f in (ROOT / "port" / "kz1hd").glob("*"):
    if f.suffix in (".txt", ".rc", ".cmake"):
        refs |= {m.rstrip("/") for m in re.findall(r"\.\./\.\./([A-Za-z0-9_./-]+)", f.read_text(errors="replace"))}
for plat, rules in (("windows", ap.RULES + ap.RULES_WINDOWS), ("linux", ap.RULES + ap.RULES_LINUX)):
    srcs = [r[0] for r in rules]
    missing = sorted(p for p in refs if (ROOT / p).exists() and
                     not any(p == s or p.startswith(s + "/") or s.startswith(p + "/") for s in srcs))
    check(not missing, f"the {plat} folder ships everything port/kz1hd's build reads above it: missing {missing}")
check("third_party/minimp3" in refs and "third_party/fidelityfx-fsr3" in refs and "VERSION" in refs,
      f"the paths are found (minimp3, FSR 3, VERSION): {sorted(refs)}")

r = subprocess.run([sys.executable, str(ROOT / "tools" / "release" / "assemble_payload.py"), "--dry-run", "--cache",
                    "/nonexistent"], capture_output=True, text=True)
check(r.returncode == 1 and "not fetched from its pin" in r.stderr, f"an empty cache is refused: {r.stderr}")

print("test_payload:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
