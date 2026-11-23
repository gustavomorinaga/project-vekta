#!/usr/bin/env python3
"""Host test for tools/release/rpcs3_disc.py (the game straight from the disc, with the official RPCS3 downloaded):
the package is found in a disc folder's PS3_EXTRA or given as a file, refused when it is an .iso, another game, the
PlayStation Store package or a damaged copy; the pinned download is checked by size and SHA-256 and a wrong one is
deleted; a stand-in RPCS3 started with exactly `--headless --installpkg` installs into the redirected settings (never
the player's), the install is judged by the files and not the exit code, RPCS3's logs (which name the licence) are
deleted, a hung RPCS3 is stopped; the installed EBOOT.BIN is decrypted into work/prep, and a second run reuses it all.
Small stand-in files replace the 3 GB package (the pins are swapped for theirs). No game data, no RPCS3, no network."""
import hashlib
import os
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import rpcs3_disc as rd  # noqa: E402
import rpcs3_import as ri  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def sha(data):
    return hashlib.sha256(data).hexdigest()


if os.name == "nt":
    print("test_rpcs3_disc: skipped on Windows (the stand-in RPCS3 is a POSIX script; the Windows paths are checked "
          "by the pins and game_dir below)")
    sys.exit(0)

# Stand-in game files and the pins swapped for them.
FILES = {"PARAM.SFO": b"sfo", "USRDIR/EBOOT.BIN": b"encrypted eboot", "USRDIR/data_ps3.psarc.edat": b"edat data",
         "ICON0.PNG": b"icon"}
rd.INSTALLED = {k: (len(v), sha(v)) for k, v in FILES.items() if k != "ICON0.PNG"}
rd.INSTALLED_FILES, rd.INSTALLED_BYTES = len(FILES), sum(len(v) for v in FILES.values())
ELF = b"plain EBOOT.BIN"
ri.EBOOT = ("EBOOT.BIN", "EBOOT.elf", len(ELF), sha(ELF))
rd.NEED_FREE = 0
REAL_PIN = dict(rd.PIN)
rd.GRACE = 1


def make_pkg(path, cid=rd.CONTENT_ID, body=b"package body"):
    head = bytearray(0x80)
    head[:4] = rd.PKG_MAGIC
    c = cid.encode()
    head[0x30:0x30 + len(c)] = c
    data = bytes(head) + body
    pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(path).write_bytes(data)
    return data


# A stand-in RPCS3: --headless --installpkg writes FILES under XDG_CONFIG_HOME/rpcs3/dev_hdd0/game/NPUA80856 and a log
# with a licence line; --decrypt makes EBOOT.elf beside its input. FAKE_MODE: ok, bad (one file differs), hang
# (installs nothing, never ends), linger (installs, then stays running).
FAKE = r'''#!/usr/bin/env python3
import os, sys, time, json
mode = os.environ.get("FAKE_MODE", "ok")
args = sys.argv[1:]
with open(os.environ["FAKE_LOG"], "a") as f:
    f.write(json.dumps({"args": args, "cfg": os.environ.get("XDG_CONFIG_HOME", ""),
                        "stdin": sys.stdin.read()}) + "\n")
if args[:2] == ["--headless", "--installpkg"]:
    if mode == "hang":
        time.sleep(60)
    cfg = os.environ["XDG_CONFIG_HOME"]
    logd = os.path.join(os.environ["XDG_CACHE_HOME"], "rpcs3")
    os.makedirs(logd, exist_ok=True)
    with open(os.path.join(logd, "RPCS3.log"), "w") as f:
        f.write("klic: (a stand-in licence line)\nSuccessfully installed the package\n")
    game = os.path.join(cfg, "rpcs3", "dev_hdd0", "game", "NPUA80856")
    for rel, data in json.loads(os.environ["FAKE_FILES"]).items():
        p = os.path.join(game, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data.encode())
    if mode == "bad":
        with open(os.path.join(game, "PARAM.SFO"), "wb") as f:
            f.write(b"SFO")
    if mode == "linger":
        time.sleep(60)
    sys.exit(143)
for i, a in enumerate(args):
    if a == "--decrypt":
        src = args[i + 1]
        with open(src[: src.rfind(".")] + ".elf", "wb") as f:
            f.write(b"plain " + os.path.basename(src).encode())
'''

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    fake = tmp / "rpcs3"
    fake.write_text(FAKE)
    fake.chmod(0o755)
    flog = tmp / "fake.log"
    os.environ.update(FAKE_LOG=str(flog), FAKE_FILES=__import__("json").dumps({k: v.decode() for k, v in FILES.items()}))
    home_cfg = tmp / "home-config"
    os.environ["XDG_CONFIG_HOME"] = str(home_cfg)   # the "player's own" settings: must stay untouched

    # ---- finding the package
    disc = tmp / "disc"
    (disc / "PS3_GAME").mkdir(parents=True)
    make_pkg(disc / "PS3_GAME" / "other.pkg", cid="UP9000-BCUS98116_00-OTHERGAME0000001")
    data = make_pkg(disc / "PS3_EXTRA" / "D0F4" / "DATA000.PKG")
    pkg, code = rd.find_pkg(disc)
    check(pkg == str(disc / "PS3_EXTRA" / "D0F4" / "DATA000.PKG") and code is None, f"found in PS3_EXTRA: {pkg} {code}")
    check(rd.find_pkg(disc / "PS3_EXTRA" / "D0F4" / "DATA000.PKG")[0], "the package file itself")
    check(rd.find_pkg(disc / "PS3_GAME" / "other.pkg") == (None, "not_pkg"), "another game's package is refused")
    (tmp / "trilogy.iso").write_bytes(b"iso")
    check(rd.find_pkg(tmp / "trilogy.iso") == (None, "iso"), "an .iso is refused with its own text")
    check(rd.find_pkg(tmp / "nothing") == (None, "no_pkg"), "a missing path")
    check(rd.find_pkg(disc / "PS3_GAME")[1] == "no_pkg", "a folder without this game's package")

    # ---- the disc's package, not the Store's or a damaged one
    rd.DISC_PKG = (len(data), sha(data))
    check(rd.check_pkg(pkg) is None, "the disc's package passes")
    psn = make_pkg(tmp / "psn" / "x.pkg", body=b"package BODY")
    check(len(psn) == len(data) and rd.check_pkg(str(tmp / "psn" / "x.pkg")) == "psn_pkg", "same size, other hash: Store")
    make_pkg(tmp / "cut" / "x.pkg", body=b"pack")
    check(rd.check_pkg(str(tmp / "cut" / "x.pkg")) == "bad_pkg", "another size: a damaged copy")
    refused = rd.prepare(str(tmp / "w-cut"), str(tmp / "cut" / "x.pkg"), exe=["unused"])
    check(refused["status"] == "problems" and refused["problems"][0]["code"] == "bad_pkg"
          and not (tmp / "w-cut" / "prep").exists(), f"a refused package leaves no work/prep: {refused}")

    # ---- the pinned download
    work = tmp / "work"
    appimage = b"#!/bin/sh\nexit 0\n"
    url = "https://github.com/RPCS3/rpcs3-binaries-linux/releases/download/build-x/rpcs3-test_linux64.AppImage"
    rd.PIN = {"version": "test", "linux": (url, len(appimage), sha(appimage)), "windows": (url, 1, "0" * 64)}
    got = []

    def dl(u, dest, body=appimage):
        got.append(u)
        pathlib.Path(dest).write_bytes(body)

    exe, code = rd.fetch(str(work), download=lambda u, d: dl(u, d, b"tampered!\n" + appimage[10:]))
    check(exe is None and code == "bad_download", f"a wrong download is refused: {code}")
    check(not list((work / "toolchain-cache" / "rpcs3").glob("*.AppImage")), "and deleted")
    exe, code = rd.fetch(str(work), download=dl)
    check(code is None and exe and os.access(exe[0], os.X_OK), f"the pinned AppImage is kept, executable: {exe}")
    check(got[-1] == url, "downloaded from the pinned URL")
    n = len(got)
    check(rd.fetch(str(work), download=dl)[0] == exe and len(got) == n, "a second run does not download again")

    def broken(u, d):
        raise OSError("no network")
    check(rd.fetch(str(tmp / "w2"), download=broken) == (None, "download"), "a failed download has its own problem")

    # ---- install + decrypt with the stand-in RPCS3
    os.environ["FAKE_MODE"] = "ok"
    res = rd.prepare(str(work), str(disc), exe=[str(fake)])
    check(res["status"] == "ok", f"prepare: {res}")
    game = work / "prep" / "disc" / "NPUA80856"
    check(res.get("game") == str(game) and (game / "USRDIR" / "EBOOT.BIN").is_file(), "the game is in work/prep/disc")
    check(res.get("eboot") == str(work / "prep" / "EBOOT.elf") and (work / "prep" / "EBOOT.elf").read_bytes() == ELF,
          "EBOOT.elf decrypted into work/prep")
    check(res.get("edat", "").endswith("data_ps3.psarc.edat"), "the .edat is the installed one (the port reads it)")
    runs = [__import__("json").loads(x) for x in flog.read_text().splitlines()]
    inst = [r for r in runs if "--installpkg" in r["args"]]
    check(inst and inst[0]["args"] == ["--headless", "--installpkg", pkg], f"exact headless argv: {inst[:1]}")
    check(inst and inst[0]["cfg"].startswith(str(work / "prep")), "RPCS3's settings in work/prep")
    check(all(r["stdin"] == "" for r in runs), "stdin is the null device")
    check(not home_cfg.exists(), "the player's own settings were not touched")
    check(not (work / "prep" / "disc-rpcs3").exists(), "RPCS3's settings and logs are removed")
    check(not (work / "prep" / "rpcs3-decrypt.log").exists(), "the decrypt step's RPCS3 log is removed too")
    leaked = [p for p in work.rglob("*") if p.is_file() and b"klic" in p.read_bytes()]
    check(not leaked, f"after the install no file keeps the licence line: {leaked}")
    check(sha(data) == rd.DISC_PKG[1] and (disc / "PS3_EXTRA" / "D0F4" / "DATA000.PKG").read_bytes() == data,
          "the disc is only read")

    n = len(flog.read_text().splitlines())
    res = rd.prepare(str(work), str(disc), exe=[str(fake)])
    check(res["status"] == "ok" and len(flog.read_text().splitlines()) == n, "a second run reuses install and EBOOT")

    # ---- failures
    for mode, want in (("bad", "install_failed"), ("hang", "install_timeout")):
        os.environ["FAKE_MODE"] = mode
        w = tmp / ("w-" + mode)
        if mode == "hang":
            r_game, probs = rd.install([str(fake)], pkg, str(w / "prep"), timeout=2)
        else:
            os.makedirs(w / "prep")
            r_game, probs = rd.install([str(fake)], pkg, str(w / "prep"))
        check(r_game is None and probs and probs[0]["code"] == want, f"{mode}: {probs}")
        check(not (w / "prep" / "disc-rpcs3").exists(), f"{mode}: nothing left behind")
    os.environ["FAKE_MODE"] = "linger"
    w = tmp / "w-linger"
    r_game, probs = rd.install([str(fake)], pkg, str(w / "prep"), timeout=30)
    check(r_game and not probs, f"an RPCS3 that stays after a good install is ended and the install kept: {probs}")

    # ---- command line
    out = tmp / "find.txt"
    rc = rd.main(["--find", "--disc", str(disc), "--out", str(out)])
    text = out.read_text(encoding="utf-8-sig")
    check(rc == 0 and "status=ok" in text and f"pkg={pkg}" in text, f"--find: {rc} {text}")

    # ---- firstrun.py --disc and the launcher
    import firstrun
    import launch
    os.environ["FAKE_MODE"] = "ok"
    real = firstrun.rpcs3_disc
    firstrun.rpcs3_disc = lambda: type("Disc", (), {"prepare": staticmethod(
        lambda work, d, log=None, step=None: rd.prepare(work, d, exe=[str(fake)], log=log, step=step))})
    c = type("C", (), {"work": str(work), "paths": {"psarc": "/old/data_ps3.psarc"}})()
    said = []
    check(firstrun.use_disc(c, str(disc), said.append) == [] and c.paths.get("eboot") == str(work / "prep" / "EBOOT.elf")
          and c.paths.get("game") == str(game) and "psarc" not in c.paths, f"use_disc: {c.paths}")
    saved = (work / "paths.env").read_text()
    check(str(game) in saved and "EBOOT.elf" in saved, f"use_disc saves the paths: {saved}")
    c2 = type("C", (), {"work": str(tmp / "w3"), "paths": {}})()
    probs = firstrun.use_disc(c2, str(tmp / "trilogy.iso"))
    check(probs and probs[0].startswith(rd.HELP["iso"][:30]), f"use_disc returns the problem's text: {probs}")
    firstrun.rpcs3_disc = real
    a = type("A", (), {"disc": str(disc), "rpcs3": "", "game": None, "eboot": None, "psarc": None, "jobs": 4,
                       "dlss": None})()
    args = launch.firstrun_args(str(tmp), a, environ={})
    check(args == ["--disc", str(disc), "--jobs", "4"], f"the launcher passes --disc on, over --rpcs3: {args}")
    check(launch.classify("disc", [rd.HELP["psn_pkg"]], 1) == launch.WRONG_EDITION, "the Store package: edition code")
    check(launch.classify("disc", [rd.HELP["no_pkg"]], 1) == launch.FILE_MISSING, "no package: the missing-file code")
    check("--disc" in launch.parser().format_help(), "the launcher takes --disc")

# ---- the pins and paths of the real thing
check(rd.CONTENT_ID.startswith("UP9000-NPUA80856"), "the content ID is Killzone HD's")
for k, suffix in (("linux", "_linux64.AppImage"), ("windows", "_win64_msvc.7z")):
    u, size, digest = REAL_PIN[k]
    check(u.startswith(f"https://github.com/RPCS3/rpcs3-binaries-{'linux' if k == 'linux' else 'win'}/releases/")
          and u.endswith(suffix) and REAL_PIN["version"] in u, f"{k}: the official RPCS3 build from its GitHub: {u}")
    check(size > 1 << 20 and len(digest) == 64 and digest == digest.lower(), f"{k}: pinned size and SHA-256")
print("test_rpcs3_disc:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
