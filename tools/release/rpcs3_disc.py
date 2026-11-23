#!/usr/bin/env python3
"""The game straight from the disc, with no RPCS3 installed: the official RPCS3, downloaded at a pinned version and
checked by SHA-256, installs the Killzone HD package of the player's own Killzone Trilogy disc (PS3_EXTRA) into a
folder of the port with no window, then decrypts a copy of its EBOOT (rpcs3_import.decrypt), as "Use my RPCS3
install" does with the player's own RPCS3.

    tools/release/rpcs3_disc.py --find    --disc PATH [--out FILE]
    tools/release/rpcs3_disc.py --prepare --disc PATH --work DIR [--rpcs3-exe EXE] [--out FILE]

PATH is the disc's folder (its PS3_EXTRA is searched for the package) or the package file itself (DATA000.PKG).
Results are `key=value` lines, as rpcs3_import.py's (UTF-8 with a BOM in --out):
  --find     status=ok|problems, pkg=, problem=code|text
  --prepare  status=ok|problems|failed, pkg=, game=, edat=, eboot=, exe=, problem=code|text
             (problems: the package or the free space was refused before anything was downloaded or written)
Exit 0 when status=ok.

Rules (the plan's route B, chosen 2026-11-19; the prototype passed on Linux and Windows 11 that day):
  - no keys and no decryption here: RPCS3 installs and decrypts, on the player's PC, from the player's own disc;
  - RPCS3 is never shipped in a release: it is downloaded from RPCS3's own GitHub releases at the version and
    SHA-256 pinned below (PIN), only when the player chooses this route, into work/toolchain-cache/rpcs3/; nothing is
    downloaded from Sony's servers;
  - the player's disc is only read, and an RPCS3 the player has is never used or touched: the install, RPCS3's
    settings and its cache all go to work/prep (RPCS3_CONFIG_DIR, XDG_CONFIG_HOME, XDG_CACHE_HOME);
  - RPCS3's exit code proves nothing (it was 143 on Linux and 0xC0000409 on Windows after a good install as well as
    a failed one): the install is judged by the files' count, size and SHA-256 (docs/GETTING-FILES.md);
  - RPCS3's log names the package's licence in plain text: it is never shown, copied or attached, only a few known
    lines are read, and it is deleted after each run (the build that follows logs its own [edat] lines, with no
    key in them).
Standard library only, Python 3.8+.
"""
import argparse
import hashlib
import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rpcs3_import as imp  # noqa: E402

WINDOWS = os.name == "nt"
TITLE = imp.TITLE
CONTENT_ID = "UP9000-NPUA80856_00-GKILLZONE1000001"
PKG_MAGIC = b"\x7fPKG"
# The Killzone HD package of the Killzone Trilogy disc (BCUS98116's PS3_EXTRA), measured 2026-11-19. The PlayStation
# Store package has the same content ID and size but another SHA-256, and needs the buyer's licence: it goes through
# "Use my RPCS3 install".
DISC_PKG = (3182645568, "94e8db935dca69b5c8b46f21196a923da957fcaf123c895c51cdc6dde4f50179")
# What the package installs (8 files, 3,182,644,484 bytes; 2026-11-19 on Linux and Windows 11), and the files checked
# by SHA-256 (docs/GETTING-FILES.md).
INSTALLED_FILES, INSTALLED_BYTES = 8, 3182644484
INSTALLED = {
    "PARAM.SFO": (1040, "352712c456e94be80fca0cd46dda271b96e81d7bf39365cf13448415dca6f642"),
    "USRDIR/EBOOT.BIN": (19708816, "942758774dc7c5f41f0a077776fde84610e186af03911e9479476b7073f005f8"),
    "USRDIR/data_ps3.psarc.edat": (3153321376, "3c85c5321bccab33079896f5e74c06902e84648154b315af21e14878d93fe58b"),
}
# The official RPCS3 build: version 0.0.43-20640 (commit 1808e241, 2026-11-19), after headless installs (c6602f6,
# 2026-04-28) and their own exit (eafa6a9, 2026-05-30). Sizes and SHA-256 as RPCS3's update API published them.
PIN = {
    "version": "0.0.43-20640",
    "linux": ("https://github.com/RPCS3/rpcs3-binaries-linux/releases/download/"
              "build-1808e241d7b1bfb1c521e392b4c51a7d5494e9ca/rpcs3-v0.0.43-20640-1808e241_linux64.AppImage",
              83306630, "8dba392a13a816a2383def9e47f3f95b67f9fd6ceacc37805b9bc00933772a80"),
    "windows": ("https://github.com/RPCS3/rpcs3-binaries-win/releases/download/"
                "build-1808e241d7b1bfb1c521e392b4c51a7d5494e9ca/rpcs3-v0.0.43-20640-1808e241_win64_msvc.7z",
                37334950, "954321803df9bd812137088b7c5cdddfb731c26301f72c3cc16c9642d22960f5"),
}
INSTALL_TIMEOUT = 15 * 60   # 34 s on a ~115 MB/s hard disk, 6 s on an SSD (2026-11-19)
GRACE = 20                  # RPCS3 still running this long after the files are all there is ended
NEED_FREE = 4 * 1024 ** 3   # the installed game (3.2 GB) and the decrypted EBOOT, with room to spare
RESULT_LINE = re.compile(r"Successfully installed|Cannot install|Failed to install|Partially installed|"
                         r"size mismatch|Aborted installation", re.I)

HELP = {
    "no_pkg": "No Killzone HD package was found there. Pick the disc's folder (the one with PS3_EXTRA in it) or the "
              "package itself (PS3_EXTRA/.../DATA000.PKG).",
    "iso": "A disc image (.iso) is not opened here. Open it with your system (double-click on Windows, or mount it "
           "on Linux) and pick the folder it shows.",
    "not_pkg": "That file is not a PS3 package of Killzone HD (NPUA80856).",
    "psn_pkg": "That is the PlayStation Store package of Killzone HD: it needs your licence, which only your own "
               "RPCS3 has. Install it in RPCS3 and choose \"Use my RPCS3 install\".",
    "bad_pkg": "The package is not the Killzone Trilogy disc's (its size or SHA-256 differs): the disc copy may be "
               "damaged. Copy it from the disc again.",
    "no_space": "Not enough free space in the install folder: about 4 GB more is needed for the game installed from "
                "the disc.",
    "download": "The official RPCS3 could not be downloaded from RPCS3's GitHub releases. Check the connection and "
                "try again, or install the game in your own RPCS3 and choose \"Use my RPCS3 install\".",
    "bad_download": "The RPCS3 download is not the pinned file (its size or SHA-256 differs); it was deleted. Try "
                    "again later.",
    "unpack": "The RPCS3 download could not be unpacked. Windows 11 unpacks it by itself; on Windows 10, install "
              "7-Zip (free, https://www.7-zip.org) and try again.",
    "install_failed": "RPCS3 did not install the package (the files below are missing or differ). Copy the package "
                      "from the disc again; a damaged copy is refused.",
    "install_timeout": "RPCS3 did not finish installing the package in time and was stopped.",
}


def _problem(code, detail=""):
    return {"code": code, "text": HELP[code] + (f" ({detail})" if detail else "")}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(4 << 20), b""):
            h.update(block)
    return h.hexdigest()


# ---------------------------------------------------------------------------------------------- the package
def pkg_content_id(path):
    """The content ID in a PS3 package's header, or None when the file is not one."""
    try:
        with open(path, "rb") as f:
            head = f.read(0x80)
    except OSError:
        return None
    if len(head) < 0x80 or head[:4] != PKG_MAGIC:
        return None
    return head[0x30:0x54].split(b"\0")[0].decode("ascii", "replace")


def find_pkg(path, depth=5):
    """The package of this game at path: the file itself, or the first .pkg under the folder (PS3_EXTRA first) whose
    header names CONTENT_ID. -> (path or None, problem code or None)."""
    path = os.path.abspath(path)
    if os.path.isfile(path):
        if path.lower().endswith(".iso"):
            return None, "iso"
        return (path, None) if pkg_content_id(path) == CONTENT_ID else (None, "not_pkg")
    if not os.path.isdir(path):
        return None, "no_pkg"
    hits = []
    for base, dirs, files in os.walk(path):
        if base[len(path):].count(os.sep) >= depth:
            dirs[:] = []
        dirs.sort(key=lambda d: (d.upper() != "PS3_EXTRA", d.upper()))
        for n in sorted(files):
            if n.lower().endswith(".pkg"):
                hits.append(os.path.join(base, n))
    for p in hits:
        if pkg_content_id(p) == CONTENT_ID:
            return p, None
    return None, "no_pkg"


def check_pkg(pkg, digest=sha256):
    """None when pkg is the disc's package, else a problem code (the hash takes about 30 s on a hard disk)."""
    size = os.path.getsize(pkg)
    if size == DISC_PKG[0] and digest(pkg) == DISC_PKG[1]:
        return None
    return "psn_pkg" if size == DISC_PKG[0] else "bad_pkg"


# ------------------------------------------------------------------------------------------- the official RPCS3
def os_key():
    return "windows" if WINDOWS else "linux"


def rpcs3_dir(work):
    return os.path.join(work, "toolchain-cache", "rpcs3", PIN["version"])


def installed_exe(work):
    """The pinned RPCS3 already fetched into work, as an argv list, or None."""
    d = rpcs3_dir(work)
    exe = os.path.join(d, "rpcs3.exe") if WINDOWS else os.path.join(d, os.path.basename(PIN["linux"][0]))
    return [exe] if os.path.isfile(exe) else None


def _unpack_7z(archive, dest):
    """RPCS3's Windows build is a solid .7z (LZMA2, BCJ2): Windows 11's own tar.exe opens it (2026-11-19), else
    7-Zip if it is installed. True when rpcs3.exe is there afterwards."""
    root = os.environ.get("SystemRoot", r"C:\Windows")
    tools = [[os.path.join(root, "System32", "tar.exe"), "-xf", archive, "-C", dest]]
    for exe in (shutil.which("7z"), *(os.path.join(os.environ[v], "7-Zip", "7z.exe")
                                     for v in ("ProgramFiles", "ProgramFiles(x86)") if os.environ.get(v))):
        if exe:
            tools.append([exe, "x", "-y", "-o" + dest, archive])
    for argv in tools:
        if not os.path.isfile(argv[0]):
            continue
        shutil.rmtree(dest, ignore_errors=True)
        os.makedirs(dest)
        try:
            subprocess.run(argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=900, creationflags=0x08000000 if WINDOWS else 0)
        except (OSError, subprocess.SubprocessError):
            continue
        if os.path.isfile(os.path.join(dest, "rpcs3.exe")):
            return True
    return False


def fetch(work, log=None, download=None, digest=sha256):
    """The pinned official RPCS3 in work/toolchain-cache/rpcs3/<version>/, downloaded when it is not there yet.
    -> (argv list or None, problem code or None)."""
    log = log or (lambda line: None)
    have = installed_exe(work)
    if have:
        return have, None
    url, size, want = PIN[os_key()]
    d = rpcs3_dir(work)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    dl = os.path.join(os.path.dirname(d), os.path.basename(url))
    if download is None:
        import fetch_toolchain
        download = fetch_toolchain.download
    log(f"rpcs3: downloading the official RPCS3 {PIN['version']} ({size / 1e6:.0f} MB) from {url}")
    try:
        download(url, dl)
    except Exception as e:   # noqa: BLE001  (URLError, a cut connection, a full disk: all the same to the player)
        log(f"rpcs3: download failed: {type(e).__name__}: {e}")
        return None, "download"
    if os.path.getsize(dl) != size or digest(dl) != want:
        os.remove(dl)
        return None, "bad_download"
    if WINDOWS:
        ok = _unpack_7z(dl, d)
        os.remove(dl)
        if not ok:
            shutil.rmtree(d, ignore_errors=True)
            return None, "unpack"
    else:
        os.makedirs(d, exist_ok=True)
        exe = os.path.join(d, os.path.basename(url))
        os.replace(dl, exe)
        os.chmod(exe, os.stat(exe).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    log(f"rpcs3: RPCS3 {PIN['version']} ready, SHA-256 checked")
    return installed_exe(work), None


# ------------------------------------------------------------------------------------------------- the install
def install_env(cfg, environ=None, windows=WINDOWS):
    """rpcs3_import's environment (settings in cfg, Windows-only PATH), and on Linux an AppImage that runs without
    FUSE (APPIMAGE_EXTRACT_AND_RUN)."""
    e = imp.rpcs3_env(cfg, environ, windows)
    if not windows:
        e["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    return e


def game_dir(cfg):
    """Where RPCS3 installs the game under its redirected settings: cfg/dev_hdd0 on Windows (RPCS3_CONFIG_DIR),
    cfg/config/rpcs3/dev_hdd0 on Linux (XDG_CONFIG_HOME)."""
    hdd0 = os.path.join(cfg, "dev_hdd0") if WINDOWS else os.path.join(cfg, "config", "rpcs3", "dev_hdd0")
    return os.path.join(hdd0, "game", TITLE)


def tree_stats(root):
    count = total = 0
    for base, _dirs, files in os.walk(root):
        for n in files:
            try:
                total += os.path.getsize(os.path.join(base, n))
                count += 1
            except OSError:
                pass
    return count, total


def check_installed(game, digest=sha256):
    """The problems of an installed game folder against INSTALLED (empty when it is the disc's game)."""
    out = []
    count, total = tree_stats(game) if os.path.isdir(game) else (0, 0)
    if (count, total) != (INSTALLED_FILES, INSTALLED_BYTES):
        out.append(f"{count} files, {total} bytes (want {INSTALLED_FILES}, {INSTALLED_BYTES})")
    for rel, (size, want) in INSTALLED.items():
        p = os.path.join(game, *rel.split("/"))
        if not os.path.isfile(p):
            out.append(f"{rel} missing")
        elif os.path.getsize(p) != size or digest(p) != want:
            out.append(f"{rel} differs")
    return out


def known_lines(cfg, out_file):
    """The result lines of RPCS3's log and captured output; a line naming the licence is never returned."""
    paths = [out_file] if os.path.isfile(out_file) else []
    for base, _dirs, files in os.walk(cfg):
        paths += [os.path.join(base, n) for n in files if n.lower() == "rpcs3.log"]
    got = []
    for p in paths:
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                got += [ln.strip()[:200] for ln in f if RESULT_LINE.search(ln) and "klic" not in ln.lower()]
        except OSError:
            pass
    return got


def scrub_logs(cfg):
    """Deletes RPCS3's logs (plain and .gz) under cfg: they name the package's licence."""
    for base, _dirs, files in os.walk(cfg):
        for n in files:
            if n.lower().startswith("rpcs3.log") or n.lower().startswith("rpcs3-install"):
                try:
                    os.remove(os.path.join(base, n))
                except OSError:
                    pass


def install(exe, pkg, prep, log=None, timeout=INSTALL_TIMEOUT, grace=GRACE, poll=0.25):
    """rpcs3 --headless --installpkg PKG with its settings in prep/disc-rpcs3, then the game folder moved to
    prep/disc/NPUA80856 and RPCS3's settings and logs removed. -> (game folder or None, problems)."""
    log = log or (lambda line: None)
    cfg, final = os.path.join(prep, "disc-rpcs3"), os.path.join(prep, "disc", TITLE)
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(cfg)
    out_file = os.path.join(cfg, "rpcs3-install.txt")
    argv = list(exe) + ["--headless", "--installpkg", pkg]   # exactly these words: RPCS3 matches the headless form
    log("rpcs3: " + " ".join(imp.loc.redact(a) if os.path.isabs(a) else a for a in argv))
    kw: dict = {"creationflags": 0x08000000} if WINDOWS else {"start_new_session": True}   # CREATE_NO_WINDOW
    game, t0, done_at, timed_out = game_dir(cfg), time.monotonic(), None, False
    with open(out_file, "wb") as out:
        proc = subprocess.Popen(argv, cwd=cfg, env=install_env(cfg), stdin=subprocess.DEVNULL, stdout=out,
                                stderr=subprocess.STDOUT, **kw)
        while proc.poll() is None:
            now = time.monotonic()
            if done_at is None and (tree_stats(game) == (INSTALLED_FILES, INSTALLED_BYTES)
                                    or known_lines(cfg, out_file)):
                done_at = now
            if (done_at is not None and now - done_at > grace) or now - t0 > timeout:
                timed_out = done_at is None
                imp._stop(proc)
                break
            time.sleep(poll)
        proc.wait()
    said = known_lines(cfg, out_file)
    log(f"rpcs3: exit {proc.returncode} after {time.monotonic() - t0:.0f} s"
        + (" (stopped at the time limit)" if timed_out else "") + "".join(f"\nrpcs3: {s}" for s in said[:4]))
    scrub_logs(cfg)
    bad = check_installed(game)
    if bad:
        shutil.rmtree(cfg, ignore_errors=True)
        return None, [_problem("install_timeout" if timed_out else "install_failed", "; ".join(bad))]
    shutil.rmtree(final, ignore_errors=True)
    os.makedirs(os.path.dirname(final), exist_ok=True)
    os.replace(game, final)
    shutil.rmtree(cfg, ignore_errors=True)
    log(f"rpcs3: the game is installed in {imp.loc.redact(final)} (8 files, SHA-256 checked)")
    return final, []


# --------------------------------------------------------------------------------------------------- together
def prepare(work, disc, exe=None, log=None, step=None):
    """find_pkg, check_pkg, fetch, install, then rpcs3_import.decrypt of the installed EBOOT.BIN into work/prep.
    -> {"status": ok|problems|failed, "pkg", "game", "edat", "eboot", "exe", "problems"}. An install already made
    (prep/disc/NPUA80856 checked) and an EBOOT.elf already decrypted are kept."""
    log = log or (lambda line: None)
    res = {"status": "problems", "problems": []}
    pkg, code = find_pkg(disc)
    if code:
        res["problems"].append(_problem(code))
        return res
    res["pkg"] = pkg
    prep = os.path.join(work, "prep")
    game = os.path.join(prep, "disc", TITLE)
    ready = os.path.isdir(game) and not check_installed(game)   # installed by an earlier run
    if not ready:
        log("rpcs3: checking the package (its SHA-256, about half a minute)")
        code = check_pkg(pkg)
        if not code:
            os.makedirs(work, exist_ok=True)
            if shutil.disk_usage(work).free < NEED_FREE:
                code = "no_space"
        if code:   # refused before anything is written: no work/prep is left behind
            res["problems"].append(_problem(code))
            return res
    os.makedirs(prep, exist_ok=True)
    res["status"] = "failed"
    if exe is None:
        exe, code = fetch(work, log)
        if code:
            res["problems"].append(_problem(code))
            return res
    res["exe"] = exe
    if not ready:
        if step:
            step(0, 2, "the game from the disc")
        game, probs = install(exe, pkg, prep, log)
        if probs:
            res["problems"] = probs
            return res
    res["game"], res["edat"] = game, os.path.join(game, "USRDIR", "data_ps3.psarc.edat")
    eboot = os.path.join(prep, imp.EBOOT[1])
    if not (os.path.isfile(eboot) and imp.sha256(eboot) == imp.EBOOT[3]):
        if step:
            step(1, 2, imp.EBOOT[1])
        try:
            d = imp.decrypt(exe, [(os.path.join(game, "USRDIR", imp.EBOOT[0]), imp.EBOOT)], prep, log=log)
        finally:   # RPCS3's own lines name the package's licence: not kept (rpcs3_import keeps them for its route)
            try:
                os.remove(os.path.join(prep, "rpcs3-decrypt.log"))
            except OSError:
                pass
        if not d["ok"]:
            res["problems"] = d["problems"]
            return res
    res["eboot"], res["status"] = eboot, "ok"
    return res


def lines(res):
    out = [f"status={res['status']}"]
    for k in ("pkg", "game", "edat", "eboot"):
        if res.get(k):
            out.append(f"{k}={res[k]}")
    if res.get("exe"):
        out.append("exe=" + " ".join(res["exe"]))
    out += [f"problem={p['code']}|{p['text']}" for p in res.get("problems", [])]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--find", action="store_true", help="only find the package (no hash, no download)")
    mode.add_argument("--prepare", action="store_true", help="fetch RPCS3, install the package, decrypt the EBOOT")
    ap.add_argument("--disc", required=True, help="the disc's folder, or its package (DATA000.PKG)")
    ap.add_argument("--work", help="the port's work folder (with --prepare)")
    ap.add_argument("--rpcs3-exe", help="an official RPCS3 to use instead of the pinned download (tests)")
    ap.add_argument("--out", help="write the result lines here (UTF-8 with a BOM) instead of stdout")
    a = ap.parse_args(argv)
    if a.find:
        pkg, code = find_pkg(a.disc)
        res = {"status": "ok", "pkg": pkg} if pkg else {"status": "problems", "problems": [_problem(code)]}
    else:
        if not a.work:
            ap.error("--prepare needs --work")
        res = prepare(os.path.abspath(a.work), a.disc, [a.rpcs3_exe] if a.rpcs3_exe else None,
                      log=lambda line: print(line, file=sys.stderr))
    text = "\n".join(lines(res)) + "\n"
    if a.out:
        with open(a.out, "w", encoding="utf-8-sig") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0 if res["status"] == "ok" else 1


if __name__ == "__main__":
    if not WINDOWS:
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    sys.exit(main())
