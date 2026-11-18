#!/usr/bin/env python3
"""Use my RPCS3 install: find Killzone HD (NPUA80856) and the PS3 system software in the player's RPCS3, and make the
two decrypted files the port needs (EBOOT.elf, libsre.prx) by asking that same RPCS3 to decrypt copies of them
(`rpcs3 --decrypt`) in a folder of the port (work/prep). The data archive needs nothing: the port reads the game's
own data_ps3.psarc.edat (patch 4014, tools/kzbuild.py data).

    tools/release/rpcs3_import.py --detect  [--rpcs3 DIR] [--out FILE]
    tools/release/rpcs3_import.py --resolve [--rpcs3 DIR] [--exe PATH] [--out FILE]
    tools/release/rpcs3_import.py --prepare [--rpcs3 DIR] [--exe PATH] --work DIR [--out FILE]

Results are `key=value` lines, on stdout or in --out (UTF-8 with a BOM, which Inno Setup reads):
  --detect   status=ok|not_found, root=, source=
  --resolve  status=ok|problems|not_found, root=, game=, edat=, dev_flash=, exe=, problem=code|text (one per line)
  --prepare  the same, then status=ok|failed, eboot=, libsre= (the decrypted copies in DIR/prep), problem=
Exit 0 when status=ok.

Rules (after Project Pyrrhus's rpcs3_import.py):
  - the player's game and RPCS3 folders are only read: the copies, RPCS3's own settings for this run and the results
    are all in DIR/prep (RPCS3_CONFIG_DIR on Windows, XDG_CONFIG_HOME and XDG_CACHE_HOME on Linux point there);
  - no keys and no decryption here: the player's RPCS3 decrypts, with what it already has;
  - RPCS3's exit code proves nothing (it is 0 after a failure too): each result is judged by its size and SHA-256
    against the tested files (docs/GETTING-FILES.md); stdin is the null device and a watchdog ends RPCS3 after
    WATCHDOG seconds without a new result (on Windows it waits on its console for a key when a file does not
    decrypt);
  - log text goes through rpcs3_locate.redact().

Built on rpcs3_locate.py, vendored unchanged from Project Pyrrhus (undecuple, MIT, tools/release/rpcs3_locate.py at
commit 1b11f54; licenses/Project-Pyrrhus-LICENSE.txt). That module is written for disc games listed in RPCS3's
games.yml; Killzone HD is a game installed in dev_hdd0/game, which it finds as an "update" folder and reports with
the problems missing_disc and no_decrypted_eboot. This wrapper drops those two for NPUA80856 and does not edit the
module. The `--decrypt` route (copies, redirected settings, null stdin, watchdog, judged by hash) follows Soulbrandt's
docs/90 (JonathanDC64, MIT); the code is written here. Standard library only, Python 3.8+ (rpcs3_locate: 3.11+ for
its own command line; the functions used here run on 3.8).
"""
import argparse
import glob
import hashlib
import os
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rpcs3_locate as loc  # noqa: E402  (vendored unchanged from Project Pyrrhus)

TITLE = "NPUA80856"
# rpcs3_locate's problems that do not apply to a game installed in dev_hdd0/game (see the docstring)
IGNORED = ("missing_disc", "no_decrypted_eboot")
# The tested files (docs/GETTING-FILES.md, tools/check-game-files.py): (input, output, size, SHA-256)
EBOOT = ("EBOOT.BIN", "EBOOT.elf", 19706080, "c81aa91e35a76ff7d856095c392cee98bcc4b01879d7f13e5a389c6ee2d903fc")
LIBSRE = ("libsre.sprx", "libsre.prx", 239344, "9e7ee6edc99b1d372686cf927b883b18f19ed4d32d1faf35e214cca4ae0737b6")
FIRMWARE = "4.93"   # the system software libsre.prx and flashMP3.pic were tested from (docs/GETTING-FILES.md)
WATCHDOG = 30
WINDOWS = os.name == "nt"
FLATPAK_ID = "net.rpcs3.RPCS3"

HELP = {
    "not_found": "No RPCS3 folder was found. Pick RPCS3's folder (where rpcs3.exe is, or its config folder on "
                 "Linux), or pick your files yourself.",
    "not_rpcs3": "That folder is not an RPCS3 folder (no rpcs3.exe, vfs.yml or games.yml).",
    "no_game": "RPCS3 has no Killzone HD (NPUA80856) installed in dev_hdd0/game. Install the game's package from "
               "the Killzone Trilogy disc in RPCS3 (see its documentation), then check again.",
    "game_incomplete": "The Killzone HD folder in RPCS3 is missing a file (named below). Install the game again in "
                       "RPCS3, then check again.",
    "no_firmware": "RPCS3 has no PS3 system software installed (no sys/external/libsre.sprx in its dev_flash). "
                   "Install the firmware in RPCS3 (see its documentation), then check again.",
    "wrong_firmware": "RPCS3 has PS3 system software {found} installed; the port needs 4.93 (its libsre and MP3 "
                      "decoder were tested from it). Install firmware 4.93 in RPCS3 (File, Install Firmware), then "
                      "check again.",
    "no_exe": "The RPCS3 program itself was not found (rpcs3.exe, the rpcs3 command, an AppImage or the Flatpak). "
              "Pick it, or pick your files yourself.",
    "no_license": "RPCS3 needs this game's licence file (a .rap in its dev_hdd0/home/00000001/exdata folder) to "
                  "decrypt the file named below, and did not find it. Your copy is the PSN version of the game: "
                  "install its licence in RPCS3 as for playing it (see RPCS3's documentation), then try again.",
    "decrypt_failed": "RPCS3 did not make the file named below. Check that this RPCS3 runs the game, then try again, "
                      "or pick your files yourself.",
    "wrong_file": "RPCS3 made the file named below, but it is not the tested one: the game must be NPUA80856 "
                  "version 01.00, and the firmware 4.93.",
}


def _problem(code, detail="", **fmt):
    return {"code": code, "text": HELP[code].format(**fmt) + (f" ({detail})" if detail else "")}


def firmware_version(dev_flash):
    """dev_flash's system software version ("4.93") from vsh/etc/version.txt, or None."""
    try:
        with open(os.path.join(dev_flash, "vsh", "etc", "version.txt"), encoding="ascii", errors="replace") as f:
            for line in f:
                if line.startswith("release:"):
                    v = line.split(":")[1]
                    return f"{int(v.split('.')[0])}.{v.split('.')[1][:2]}"
    except (OSError, ValueError, IndexError):
        pass
    return None


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# ---------------------------------------------------------------------------------------------------- the program
def find_exe(root, given=None, home=None):
    """The command that starts the player's RPCS3, as an argv prefix, or None. given (a file the player picked)
    first; then rpcs3.exe in the RPCS3 folder (Windows and portable installs); then the rpcs3 command, an AppImage in
    the usual folders and the Flatpak."""
    if given:
        return [given] if os.path.isfile(given) else None
    home = home or os.path.expanduser("~")
    if root:
        for name in ("rpcs3.exe", "rpcs3"):
            p = os.path.join(root, name)
            if os.path.isfile(p):
                return [p]
    if WINDOWS:
        return None
    p = shutil.which("rpcs3")
    if p:
        return [p]
    images = []
    for d in ("Applications", ".local/bin", "Downloads", "bin", "Games", "AppImages"):
        images += glob.glob(os.path.join(home, d, "*[Rr][Pp][Cc][Ss]3*.AppImage"))
    if images:
        return [max(images, key=os.path.getmtime)]   # the newest one
    if shutil.which("flatpak"):
        r = subprocess.run(["flatpak", "info", FLATPAK_ID], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            return ["flatpak", "run", FLATPAK_ID]
    return None


# -------------------------------------------------------------------------------------------------- resolve
def rpcs3_root_of(path, depth=6):
    """The RPCS3 folder a path lies in (the first of it and its parents, up to depth levels, that rpcs3_locate takes
    for RPCS3), else None. A player who points at RPCS3's dev_hdd0/game/NPUA80856, dev_flash or any file in them
    means that RPCS3: rpcs3_locate itself only climbs out of config/, dev_hdd0/ and dev_flash/."""
    if not path:
        return None
    p = os.path.abspath(os.path.expanduser(str(path)))
    if os.path.isfile(p):
        p = os.path.dirname(p)
    for _ in range(depth + 1):
        if os.path.isdir(p) and loc.is_rpcs3(p):
            return p
        up = os.path.dirname(p)
        if up == p:
            break
        p = up
    return None


# Where a portable RPCS3 is unzipped one level deeper than rpcs3_locate looks (it checks D:\Games\*rpcs3* and
# D:\PS3\*rpcs3*, not D:\Games\PS3\*rpcs3*): <fixed drive or home>\<one of these>\<one of these>\*rpcs3*\
NESTED = ("Games", "Emulators", "Emulator", "Emulation", "PS3")


def nested_portables(bases=None):
    """Portable RPCS3 folders (rpcs3.exe beside them) two folders down a drive root or the home folder, as
    D:/Games/PS3/rpcs3-v0.0.28-...; Windows only, reads directory listings only."""
    if bases is None:
        if not WINDOWS:
            return []
        bases = [d for d, _ in loc._portable_bases() if os.path.splitdrive(d)[1] in ("\\", "/")]
        if os.environ.get("USERPROFILE"):
            bases.append(os.environ["USERPROFILE"])
    out = []
    for b in bases:
        for a in NESTED:
            for c in NESTED:
                if a == c:
                    continue
                for exe in sorted(glob.glob(os.path.join(glob.escape(b), a, c, "*[Rr][Pp][Cc][Ss]3*", "rpcs3.exe"))):
                    d = os.path.dirname(exe)
                    if d not in out:
                        out.append(d)
    return out


def _hints(hint):
    """The hints to try in turn: the RPCS3 folder the given path lies in (or the path itself); with none given, the
    usual places (None) and then the nested portable folders. A given path that is in no RPCS3 folder (a game folder
    that RPCS3's vfs.yml puts elsewhere) is tried as it is, then the usual places; see _game_of."""
    if hint:
        root = rpcs3_root_of(hint)
        if root:
            return [root]
        # only a game folder falls back to the usual places; any other folder must be RPCS3 itself, so a wrong pick
        # says "not an RPCS3 folder" instead of quietly using another RPCS3
        return [hint] + [None] + nested_portables() if _game_of(hint) else [hint]
    return [None] + nested_portables()


def _game_of(hint):
    """The game folder a hint names when it is not in an RPCS3 folder: the hint itself or the folder it is in, when
    a PARAM.SFO is there (else None). An RPCS3 found elsewhere counts only when its NPUA80856 is that folder."""
    if not hint or rpcs3_root_of(hint):
        return None
    p = os.path.abspath(os.path.expanduser(str(hint)))
    for d in (p, os.path.dirname(p), os.path.dirname(os.path.dirname(p))):
        if os.path.isfile(os.path.join(d, "PARAM.SFO")):
            return os.path.normcase(os.path.realpath(d))
    return None


def _game_path(res):
    t = res["titles"].get(TITLE) or {}
    upd = t.get("update") or {}
    return upd.get("path") if upd.get("ok") else next((i["path"] for i in t.get("installs") or []
                                                       if i.get("name") == TITLE), "")


def _has_game(res):
    t = res["titles"].get(TITLE) or {}
    return bool((t.get("update") or {}).get("ok") or any(i.get("name") == TITLE for i in t.get("installs") or []))


def detect(hint=None):
    game = _game_of(hint)
    for h in _hints(hint):
        if game and h != hint:
            r = loc.locate(h, {TITLE: None})
            if not (r["ok"] and _same(_game_path(r), game)):
                continue
            return {"status": "ok", "root": r["root"], "source": r["source"]}
        # rpcs3_locate lists the usual places after a given folder: with one given, only that folder counts
        cands = [c for c in loc.find_rpcs3(h) if c.get("root") and (h is None or c.get("source") == "hint")]
        if cands:
            return {"status": "ok", "root": cands[0]["root"], "source": cands[0]["source"]}
    return {"status": "not_found"}


def _same(a, b):
    return bool(a) and os.path.normcase(os.path.realpath(a)) == b


def resolve(hint=None, exe=None, home=None):
    """What the player's RPCS3 has for the port: {"status", "root", "game", "edat", "dev_flash", "exe" (argv),
    "problems": [{"code", "text"}]}. Reads only."""
    res = None
    game = _game_of(hint)
    for h in _hints(hint):
        r = loc.locate(h, {TITLE: None})
        if game and h != hint and not (r["ok"] and _same(_game_path(r), game)):
            continue   # the game folder was given: only the RPCS3 that has that very folder
        if r["ok"] and _has_game(r):
            res = r
            break
        if res is None or (r["ok"] and not res["ok"]):
            res = r   # the first RPCS3 found (without the game) explains the problem best
    out = {"status": "not_found", "root": res.get("root"), "dev_hdd0": res.get("dev_hdd0") or "", "game": "", "edat": "",
           "dev_flash": res.get("dev_flash")
           or "", "exe": None, "problems": []}
    if not res["ok"]:
        code = "not_rpcs3" if res.get("code") == "not_rpcs3" else "not_found"
        out["problems"].append(_problem(code))
        return out
    t = res["titles"][TITLE]
    upd = t.get("update") or {}
    game = upd.get("path") if upd.get("ok") else next((i["path"] for i in t.get("installs") or []
                                                       if i.get("name") == TITLE), "")
    probs = [p for p in t["problems"] if p["code"] not in IGNORED + ("missing_update",)]
    if not game:
        probs = [_problem("no_game")]
    else:
        out["game"] = game
        missing = [n for n in ("PARAM.SFO", "USRDIR/EBOOT.BIN", "USRDIR/data_ps3.psarc.edat")
                   if not os.path.isfile(os.path.join(game, n))]
        if missing:
            probs.append(_problem("game_incomplete", ", ".join(missing)))
        out["edat"] = os.path.join(game, "USRDIR", "data_ps3.psarc.edat")
    fw = firmware_version(out["dev_flash"])
    out["firmware"] = fw or ""
    if not os.path.isfile(os.path.join(out["dev_flash"], "sys", "external", LIBSRE[0])) or not fw:
        probs.append(_problem("no_firmware"))
    elif fw != FIRMWARE:
        probs.append(_problem("wrong_firmware", found=fw))
    out["exe"] = find_exe(res["root"], exe, home)
    if not out["exe"]:
        probs.append(_problem("no_exe"))
    out["problems"] = probs
    out["status"] = "problems" if probs else "ok"
    return out


# -------------------------------------------------------------------------------------------------- decrypt
def rpcs3_env(cfg, environ=None):
    """The environment for a child RPCS3 whose settings, cache and log go to cfg, not to the player's own."""
    e = dict(os.environ if environ is None else environ)
    e["QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES"] = "1"
    if WINDOWS:
        e["RPCS3_CONFIG_DIR"] = os.path.join(cfg, "")   # RPCS3 cuts the value after its last separator
    else:
        e["XDG_CONFIG_HOME"] = os.path.join(cfg, "config")
        e["XDG_CACHE_HOME"] = os.path.join(cfg, "cache")
        e.setdefault("QT_QPA_PLATFORM", "offscreen")
    return e


def _argv(exe, cfg, inputs):
    argv = list(exe)
    if argv[:2] == ["flatpak", "run"]:
        argv[2:2] = [f"--filesystem={os.path.dirname(cfg)}", f"--env=XDG_CONFIG_HOME={cfg}/config",
                     f"--env=XDG_CACHE_HOME={cfg}/cache"]
    for p in inputs:
        argv += ["--decrypt", p]   # never "--decrypt=": that form misses RPCS3's headless path
    return argv


def _stop(proc):
    try:
        if WINDOWS:
            proc.kill()
        else:
            os.killpg(proc.pid, signal.SIGKILL)   # the AppImage's runtime and RPCS3 under it: our group only
    except OSError:
        pass


EXDATA = ("home", "00000001", "exdata")   # where RPCS3 looks for a game's licence (.rap) for its first user


def _link_dir(link, target):
    """A directory link (a symbolic link, or on Windows without that right a junction, which needs none)."""
    os.makedirs(os.path.dirname(link), exist_ok=True)
    try:
        os.symlink(target, link, target_is_directory=True)
    except OSError:
        if not WINDOWS:
            raise
        import _winapi
        _winapi.CreateJunction(target, link)


def _unlink_dir(link):
    """Removes the link itself, never what it points at."""
    try:
        if os.path.islink(link):
            os.unlink(link)
        elif os.path.isdir(link):
            os.rmdir(link)   # a junction: rmdir removes the link, and refuses a real folder that is not empty
    except OSError:
        pass


def _exdata_links(cfg, hdd0):
    """Where RPCS3 looks for exdata under the redirected settings (RPCS3_CONFIG_DIR's dev_hdd0 on Windows,
    XDG_CONFIG_HOME/rpcs3/dev_hdd0 on Linux), linked to the player's own dev_hdd0/home/00000001/exdata: a PSN copy
    of the game needs its .rap to decrypt the EBOOT, and RPCS3 reads it through the link. This module never opens a
    .rap, and RPCS3's own start-up folders go to cfg, not into the player's dev_hdd0. Returns the links made."""
    target = os.path.join(hdd0, *EXDATA)
    if not os.path.isdir(target):
        return []
    links = [os.path.join(cfg, "dev_hdd0", *EXDATA), os.path.join(cfg, "config", "rpcs3", "dev_hdd0", *EXDATA)]
    for link in links:
        _link_dir(link, target)
    return links


def _run_one(exe, cfg, src, out, prep, logf, watchdog):
    """One RPCS3 --decrypt run on one copy (RPCS3 stops a batch at its first failure). -> (exit code, timed out)."""
    argv = _argv(exe, cfg, [src])
    kw: dict = {"creationflags": 0x08000000} if WINDOWS else {"start_new_session": True}   # CREATE_NO_WINDOW
    proc = subprocess.Popen(argv, cwd=prep, env=rpcs3_env(cfg), stdin=subprocess.DEVNULL, stdout=logf,
                            stderr=subprocess.STDOUT, **kw)
    t0 = time.monotonic()
    while proc.poll() is None:
        if not os.path.isfile(out) and time.monotonic() - t0 > watchdog:
            _stop(proc)
            proc.wait()
            return proc.returncode, True
        time.sleep(0.25)
    return proc.returncode, False


def decrypt(exe, sources, prep, watchdog=WATCHDOG, log=None, hdd0=None):
    """Copies each (source path, (in name, out name, size, sha)) into prep/in, runs RPCS3 --decrypt on each copy and
    checks each output. hdd0: the player's dev_hdd0, for the licence RPCS3 may need (see _exdata_links). Returns {"ok",
    "files": {out name: path}, "problems"}. The copies and RPCS3's settings are removed afterwards; the good outputs
    are moved to prep; RPCS3's own lines are kept in prep/rpcs3-decrypt.log."""
    log = log or (lambda line: None)
    inp, cfg = os.path.join(prep, "in"), os.path.join(prep, "rpcs3")
    shutil.rmtree(inp, ignore_errors=True)
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(inp)
    os.makedirs(cfg)
    links = []
    if hdd0:
        try:
            links = _exdata_links(cfg, hdd0)
        except OSError as e:
            log(f"rpcs3: no link to the licence folder ({e}); a PSN copy of the game may not decrypt")
    res = {"ok": True, "files": {}, "problems": []}
    logpath = os.path.join(prep, "rpcs3-decrypt.log")
    with open(logpath, "wb") as logf:
        for src, (in_name, out_name, size, digest) in sources:
            copy, out = os.path.join(inp, in_name), os.path.join(inp, out_name)
            shutil.copyfile(src, copy)
            log("rpcs3: " + " ".join(loc.redact(a) if os.path.isabs(a) else a for a in _argv(exe, cfg, [copy])))
            logf.flush()
            before = logf.tell()
            rc, timed_out = _run_one(exe, cfg, copy, out, prep, logf, watchdog)
            log(f"rpcs3: exit {rc}" + (f" (stopped after {watchdog} s without the file)" if timed_out else ""))
            if not os.path.isfile(out):
                with open(logpath, "rb") as f:
                    f.seek(before)
                    said = f.read().decode("utf-8", "replace")
                code = "no_license" if "license file" in said or ".rap" in said else "decrypt_failed"
                res["problems"].append(_problem(code, out_name))
                continue
            got_size, got = os.path.getsize(out), sha256(out)
            log(f"rpcs3: {out_name} {got_size} bytes, SHA-256 {got[:16]}...")
            if got_size != size or got != digest:
                res["problems"].append(_problem("wrong_file", f"{out_name}: {got_size} bytes, SHA-256 {got[:16]}..."))
                continue
            final = os.path.join(prep, out_name)
            os.replace(out, final)
            res["files"][out_name] = final
    for link in links:
        _unlink_dir(link)   # first, so the clean-up below cannot reach the player's folder through it
    shutil.rmtree(inp, ignore_errors=True)
    shutil.rmtree(cfg, ignore_errors=True)
    res["ok"] = not res["problems"]
    return res


def prepare(work, hint=None, exe=None, log=None, watchdog=WATCHDOG):
    """resolve(), then decrypt EBOOT.BIN and libsre.sprx into work/prep. The result of resolve() plus "eboot" and
    "libsre" (paths) and status ok|failed."""
    r = resolve(hint, exe)
    if r["status"] != "ok":
        return r
    prep = os.path.join(work, "prep")
    os.makedirs(prep, exist_ok=True)
    have = {spec[1]: os.path.join(prep, spec[1]) for spec in (EBOOT, LIBSRE)}
    want = [(src, spec) for src, spec in ((os.path.join(r["game"], "USRDIR", EBOOT[0]), EBOOT),
                                          (os.path.join(r["dev_flash"], "sys", "external", LIBSRE[0]), LIBSRE))
            if not (os.path.isfile(have[spec[1]]) and sha256(have[spec[1]]) == spec[3])]   # kept from a last run
    d = decrypt(r["exe"], want, prep, watchdog, log, r.get("dev_hdd0")) if want else {"ok": True, "problems": []}
    r["problems"] = d["problems"]
    r["status"] = "ok" if d["ok"] else "failed"
    if d["ok"]:
        r["eboot"], r["libsre"] = have["EBOOT.elf"], have["libsre.prx"]
    return r


# -------------------------------------------------------------------------------------------------- command line
def lines(res):
    out = [f"status={res['status']}"]
    for k in ("root", "source", "game", "edat", "dev_flash", "firmware", "eboot", "libsre"):
        if res.get(k):
            out.append(f"{k}={res[k]}")
    if res.get("exe"):
        out.append("exe=" + " ".join(res["exe"]))
    out += [f"problem={p['code']}|{p['text']}" for p in res.get("problems", [])]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--detect", action="store_true")
    g.add_argument("--resolve", action="store_true")
    g.add_argument("--prepare", action="store_true")
    ap.add_argument("--rpcs3", default="", help="RPCS3's folder (default: the usual places)")
    ap.add_argument("--exe", default="", help="the RPCS3 program (default: found from the folder, PATH, AppImages, "
                                              "the Flatpak)")
    ap.add_argument("--work", help="with --prepare: the port's work folder (the files go to WORK/prep)")
    ap.add_argument("--out", help="write the key=value lines here (UTF-8 with a BOM) instead of stdout")
    a = ap.parse_args(argv)
    if a.prepare and not a.work:
        ap.error("--prepare needs --work")
    if a.detect:
        res = detect(a.rpcs3)
    elif a.resolve:
        res = resolve(a.rpcs3, a.exe or None)
    else:
        res = prepare(a.work, a.rpcs3, a.exe or None, log=lambda line: print(line, file=sys.stderr))
    text = "\n".join(lines(res)) + "\n"
    if a.out:   # written aside and renamed: the setup polls for the file and must never read half of it
        with open(a.out + ".tmp", "w", encoding="utf-8-sig", newline="\r\n" if WINDOWS else "\n") as f:
            f.write(text)
        os.replace(a.out + ".tmp", a.out)
    else:
        sys.stdout.write(text)
    return 0 if res["status"] == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
