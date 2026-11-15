#!/usr/bin/env python3
"""Host test for tools/release/rpcs3_import.py ("use my RPCS3 install") and firstrun.py's --rpcs3: in a made-up RPCS3
folder with Killzone HD installed in dev_hdd0/game, the wrapper finds the game, its .edat and dev_flash, drops the
two problems rpcs3_locate.py reports for a game that is not a disc, runs a stand-in RPCS3 with --decrypt on copies in
work/prep (its settings redirected there, stdin the null device), judges the results by hash, stops a hung RPCS3
with the watchdog, and never writes into the game's or RPCS3's folders. Also: rpcs3_locate.py is still the copy
vendored from Project Pyrrhus. No game data, no RPCS3, no network."""
import hashlib
import io
import os
import pathlib
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import fake_game_files  # noqa: E402
import firstrun  # noqa: E402
import rpcs3_import as ri  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


# rpcs3_locate.py is vendored unchanged from Project Pyrrhus (tools/release/rpcs3_locate.py at commit 1b11f54): its
# fixes are made upstream and the file is copied over whole, never edited here. A new copy changes this hash.
got = hashlib.sha256((ROOT / "tools" / "release" / "rpcs3_locate.py").read_bytes()).hexdigest()
check(got == "5ea2c6e42c0eb8e78abcd57381302494f86394c3a24d02bad0bcaab7bd9129ea",
      f"rpcs3_locate.py is Project Pyrrhus's copy, unedited: {got}")

# A stand-in RPCS3: records its environment and stdin, then makes <name>.elf / <name>.prx beside each --decrypt input
# (as RPCS3 does). FAKE_MODE: ok, bad (wrong contents), hang (never finishes), none (makes nothing).
FAKE = r'''#!/usr/bin/env python3
import os, sys, time
mode = os.environ.get("FAKE_MODE", "ok")
with open(os.environ["FAKE_LOG"], "a") as f:
    ex = os.path.join(os.environ.get("XDG_CONFIG_HOME", ""), "rpcs3", "dev_hdd0", "home", "00000001", "exdata")
    f.write("|".join([os.environ.get("XDG_CONFIG_HOME", ""), os.environ.get("XDG_CACHE_HOME", ""),
                      repr(sys.stdin.read()), " ".join(sys.argv[1:]),
                      ",".join(sorted(os.listdir(ex))) if os.path.isdir(ex) else "-"]) + "\n")
if mode == "norap":
    print("RPCS3: SELF: Failed to locate the game license file: x/exdata/UP9000-NPUA80856_00-X.rap.")
if mode == "hang":
    time.sleep(60)
args = sys.argv[1:]
for i, a in enumerate(args):
    if a == "--decrypt" and mode not in ("none", "norap"):
        src = args[i + 1]
        out = src[: src.rfind(".")] + (".elf" if src.endswith(".BIN") else ".prx")
        with open(out, "wb") as f:
            f.write(b"plain " + os.path.basename(src).encode() + (b" bad" if mode == "bad" else b""))
'''


def tree_state(path):
    """Every file and folder under path with its size and mtime: proof that nothing was written there."""
    out = {}
    for d, dirs, files in os.walk(path):
        for n in dirs + files:
            p = os.path.join(d, n)
            st = os.stat(p)
            out[p] = (st.st_size, st.st_mtime_ns)
    return out


def spec(name, out):
    data = b"plain " + name.encode()
    return (name, out, len(data), hashlib.sha256(data).hexdigest())


with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    rp = tmp / "RPCS3"                       # a portable RPCS3 folder
    (rp / "config").mkdir(parents=True)
    (rp / "config" / "vfs.yml").write_text("/dev_hdd0/: $(EmulatorDir)dev_hdd0/\n/dev_flash/: $(EmulatorDir)dev_flash/\n")
    (rp / "config" / "games.yml").write_text("")      # no disc listed: Killzone HD is an HDD game
    game = rp / "dev_hdd0" / "game" / "NPUA80856"
    (game / "USRDIR").mkdir(parents=True)
    (game / "PARAM.SFO").write_bytes(fake_game_files.sfo({"APP_VER": "01.00", "CATEGORY": "HG",
                                                          "TITLE_ID": "NPUA80856"}))
    (game / "USRDIR" / "EBOOT.BIN").write_bytes(b"SCE\0 encrypted eboot")
    (game / "USRDIR" / "data_ps3.psarc.edat").write_bytes(b"NPD\0 archive")
    ext = rp / "dev_flash" / "sys" / "external"
    ext.mkdir(parents=True)
    (ext / "libsre.sprx").write_bytes(b"SCE\0 encrypted libsre")
    ver = rp / "dev_flash" / "vsh" / "etc" / "version.txt"
    ver.parent.mkdir(parents=True)
    ver.write_text("release:04.9300:\nbuild:1,20260101:x\n")
    exdata = rp / "dev_hdd0" / "home" / "00000001" / "exdata"   # the player's licences (a made-up name; never read)
    exdata.mkdir(parents=True)
    (exdata / "UP9000-TEST00000_00-NOTAREALLICENCE.rap").write_bytes(b"x" * 16)
    exe = rp / "rpcs3"
    exe.write_text(FAKE)
    exe.chmod(0o755)
    flog = tmp / "fake.log"
    os.environ["FAKE_LOG"] = str(flog)
    ri.EBOOT, ri.LIBSRE = spec("EBOOT.BIN", "EBOOT.elf"), spec("libsre.sprx", "libsre.prx")
    before = tree_state(rp)

    # resolve: found, the HDD game's two disc problems dropped, the program in the folder
    r = ri.resolve(str(rp))
    check(r["status"] == "ok" and r["problems"] == [], f"resolve: ok, no problems: {r['problems']}")
    check(r["game"] == str(game) and r["edat"] == str(game / "USRDIR" / "data_ps3.psarc.edat")
          and r["dev_flash"] == str(rp / "dev_flash") and r["exe"] == [str(exe)], f"resolve: the paths: {r}")
    check(ri.lines(r)[0] == "status=ok" and f"game={game}" in ri.lines(r), "resolve: key=value lines")

    # prepare: the copies decrypted in work/prep, settings redirected there, stdin empty, folders untouched
    work = tmp / "work"
    p = ri.prepare(str(work), str(rp))
    prep = work / "prep"
    check(p["status"] == "ok" and p["eboot"] == str(prep / "EBOOT.elf") and p["libsre"] == str(prep / "libsre.prx"),
          f"prepare: ok, the two files in work/prep: {p}")
    check((prep / "EBOOT.elf").read_bytes() == b"plain EBOOT.BIN", "prepare: the EBOOT made by RPCS3 is kept")
    call = flog.read_text().splitlines()
    cfg = str(prep / "rpcs3")
    check(len(call) == 2 and call[0].startswith(f"{cfg}/config|{cfg}/cache|''|--decrypt {prep}/in/EBOOT.BIN|")
          and call[1].startswith(f"{cfg}/config|{cfg}/cache|''|--decrypt {prep}/in/libsre.sprx|"),
          f"prepare: one RPCS3 run per copy (a failure stops a batch), settings in work/prep, stdin empty: {call}")
    check(all(c.endswith("|UP9000-TEST00000_00-NOTAREALLICENCE.rap") for c in call),
          f"RPCS3 sees the player's exdata (licences) through a link in its redirected settings: {call}")
    check(not (prep / "in").exists() and not (prep / "rpcs3").exists(), "prepare: the copies and settings are removed")
    check(tree_state(rp) == before, "prepare: nothing written into RPCS3's or the game's folders")
    p = ri.prepare(str(work), str(rp))
    check(p["status"] == "ok" and len(flog.read_text().splitlines()) == 2, "prepare again: the kept files are reused")
    check((exdata / "UP9000-TEST00000_00-NOTAREALLICENCE.rap").read_bytes() == b"x" * 16,
          "the player's licence folder is still there after the clean-up (only the link was removed)")

    # results judged by their hash, not by RPCS3's exit code
    for f in ("EBOOT.elf", "libsre.prx"):
        (prep / f).unlink()
    os.environ["FAKE_MODE"] = "bad"
    p = ri.prepare(str(work), str(rp))
    check(p["status"] == "failed" and [x["code"] for x in p["problems"]] == ["wrong_file", "wrong_file"]
          and not (prep / "EBOOT.elf").exists(), f"a result with another hash is refused: {p['problems']}")
    os.environ["FAKE_MODE"] = "none"
    p = ri.prepare(str(work), str(rp))
    check(p["status"] == "failed" and [x["code"] for x in p["problems"]] == ["decrypt_failed", "decrypt_failed"],
          f"no result (exit 0 all the same): decrypt_failed: {p['problems']}")
    os.environ["FAKE_MODE"] = "norap"
    p = ri.prepare(str(work), str(rp))
    check(p["status"] == "failed" and p["problems"][0]["code"] == "no_license" and ".rap" in p["problems"][0]["text"],
          f"RPCS3 asking for the .rap is named as the missing licence: {p['problems']}")
    os.environ["FAKE_MODE"] = "hang"
    t0 = time.monotonic()
    p = ri.prepare(str(work), str(rp), watchdog=1)
    check(p["status"] == "failed" and time.monotonic() - t0 < 20, "the watchdog stops an RPCS3 that never finishes")
    os.environ["FAKE_MODE"] = "ok"
    check(tree_state(rp) == before, "after the failures too, nothing written into RPCS3's or the game's folders")

    # the firmware's version: another one than 4.93 is named, with the version found
    ver.write_text("release:04.9000:\n")
    r = ri.resolve(str(rp))
    check([x["code"] for x in r["problems"]] == ["wrong_firmware"] and "4.90" in r["problems"][0]["text"]
          and "4.93" in r["problems"][0]["text"], f"firmware 4.90: wrong_firmware names both versions: {r['problems']}")
    ver.unlink()
    check([x["code"] for x in ri.resolve(str(rp))["problems"]] == ["no_firmware"], "no version.txt: no_firmware")
    ver.write_text("release:04.9300:\n")
    check(ri.resolve(str(rp))["firmware"] == "4.93", "resolve reports the firmware's version")

    # what resolve reports when a piece is missing
    (ext / "libsre.sprx").rename(tmp / "libsre.sprx")
    check([x["code"] for x in ri.resolve(str(rp))["problems"]] == ["no_firmware"], "no libsre.sprx: no_firmware")
    (tmp / "libsre.sprx").rename(ext / "libsre.sprx")
    (game / "USRDIR" / "data_ps3.psarc.edat").rename(tmp / "x.edat")
    r = ri.resolve(str(rp))
    check([x["code"] for x in r["problems"]] == ["game_incomplete"] and "data_ps3.psarc.edat" in r["problems"][0]["text"],
          f"no .edat: game_incomplete names it: {r['problems']}")
    (tmp / "x.edat").rename(game / "USRDIR" / "data_ps3.psarc.edat")
    check(ri.resolve(str(tmp / "nowhere"))["problems"][0]["code"] in ("not_rpcs3", "not_found"),
          "a folder that is not RPCS3 is named so")

    # the command line: --out in UTF-8 with a BOM, exit 0 on ok
    out = tmp / "out.txt"
    rc = ri.main(["--resolve", "--rpcs3", str(rp), "--out", str(out)])
    check(rc == 0 and out.read_bytes().startswith(b"\xef\xbb\xbfstatus=ok"), "--resolve --out: BOM, status=ok")

    # firstrun --rpcs3: the paths come from RPCS3 and are saved; the interactive offer skips the questions
    c = firstrun.Ctx(root=str(ROOT), paths={}, work=str(tmp / "work2"))
    said = []
    check(firstrun.use_rpcs3(c, str(rp), said.append) == [] and c.paths["eboot"] == str(tmp / "work2" / "prep" /
          "EBOOT.elf") and c.paths["game"] == str(game) and c.paths["dev_flash"] == str(rp / "dev_flash"),
          f"use_rpcs3: the paths: {c.paths}")
    saved = firstrun.read_paths(c.work)
    check(saved.get("libsre") == str(tmp / "work2" / "prep" / "libsre.prx"), f"use_rpcs3: saved in paths.env: {saved}")
    c = firstrun.Ctx(root=str(ROOT), paths={}, work=str(tmp / "work3"))
    answers = iter([""])
    check(firstrun.offer_rpcs3(c, str(rp), ask=lambda q: next(answers), say=said.append) and
          c.paths.get("eboot", "").endswith("EBOOT.elf"), "the interactive offer, accepted, fills the paths")
    c = firstrun.Ctx(root=str(ROOT), paths={}, work=str(tmp / "work4"))
    check(not firstrun.offer_rpcs3(c, str(rp), ask=lambda q: "n", say=said.append) and "eboot" not in c.paths,
          "the interactive offer, declined, leaves the paths to the questions")
    err = io.StringIO()
    so, sys.stderr = sys.stderr, err
    try:
        os.environ["KZ_WORK"] = str(tmp / "work5")
        so2, sys.stdout = sys.stdout, io.StringIO()
        try:
            rc = firstrun.main(["--rpcs3", str(tmp / "nowhere"), "--root", str(ROOT), "--status-file",
                                str(tmp / "status")])
        finally:
            sys.stdout = so2
        del os.environ["KZ_WORK"]
    finally:
        sys.stderr = so
    check(rc == 1 and "cannot be used" in err.getvalue(), f"firstrun --rpcs3 with no RPCS3 stops and says why: {rc}")
    st = (tmp / "status").read_text().splitlines() if (tmp / "status").exists() else []
    check(len(st) >= 3 and st[0].startswith("LOG ") and st[1].startswith("RESULT ok=0 step=rpcs3")
          and st[2].startswith("REASON your RPCS3 cannot be used"),
          f"and its status file ends with RESULT and the reason, which the setup's build page waits for: {st}")

print("test_rpcs3_import:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
