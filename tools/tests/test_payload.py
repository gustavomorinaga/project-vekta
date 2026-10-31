#!/usr/bin/env python3
"""Host test for tools/release/assemble_payload.py and tools/release/launch.py: the payload of this repository (with
a stand-in toolchain cache) keeps the repository's layout, holds only the allowlist, the shipped patched ps3recomp and
the Windows toolchain entries with their own fetched.json, gives the embeddable Python's ._pth the tools folders, and
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
    files, gen, man = ap.plan(str(ROOT), str(cache))
    paths = set(files) | set(gen)
    for need in ("tools/kzbuild.py", "tools/release/firstrun.py", "tools/release/launch.py", "run.cmd", "play.cmd",
                 "port/kz1hd/CMakeLists.txt", "port/cmake/llvm-mingw.cmake", "re/function_seeds.txt", "VERSION",
                 "work/ps3recomp-patched/.patchset", "work/ps3recomp-patched/tools/ppu_lifter.py",
                 "work/toolchain-cache/fetched.json", "work/toolchain-cache/llvm-mingw/bin/llvm-mingw.exe"):
        check(need in paths, f"the payload has {need}")
    check(not any(p.startswith(("third_party/", "port/patches/", ".github/", "work/toolchain-cache/downloads/",
                                "work/toolchain-cache/llvm-mingw-linux/", "work/ps3recomp-patched/docs/"))
                  for p in paths), "no submodule, patches, workflows, downloads, Linux compiler or ps3recomp docs")
    check(not any("__pycache__" in p or p.endswith(".pyc") for p in paths), "no Python caches")
    tc = {e["name"] for e in ft.entries_for(m, "windows")}
    check(json.loads(gen["work/toolchain-cache/fetched.json"]) == {n: e["sha256"] for e in m["components"]
                                                                   for n in [e["name"]] if n in tc},
          "fetched.json holds the Windows entries only")
    pth = gen["work/toolchain-cache/python/python313._pth"].decode()
    check(pth.split("\r\n")[:2] == ["python313.zip", "."] and "..\\..\\..\\tools" in pth and
          "..\\..\\ps3recomp-patched\\tools" in pth and "#" not in pth, f"the ._pth names the tools folders: {pth!r}")
    check(man["count"] == len(paths) and man["patchset"] and set(man) == {"format", "version", "commit", "patchset", "toolchain", "count",
                                                        "bytes", "files"}
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

r = subprocess.run([sys.executable, str(ROOT / "tools" / "release" / "assemble_payload.py"), "--dry-run", "--cache",
                    "/nonexistent"], capture_output=True, text=True)
check(r.returncode == 1 and "not fetched from its pin" in r.stderr, f"an empty cache is refused: {r.stderr}")

print("test_payload:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
