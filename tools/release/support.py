#!/usr/bin/env python3
"""The support package: one zip a player attaches to a bug report.

    launch.py --support-package [--out DIR]      (Start menu: "Make a support package"; default DIR: the Desktop)

After Project Helghast's tools/release/support.py (undecuple, MIT), from which the idea, the file list's shape and the
trimming come. It holds text only, all through firstrun.redactor() (the home folder becomes ~): the last game log
(work/play.log), the newest two first-run logs (work/logs/firstrun-*.log), vekta-install.ini, work/build-stamp.json,
the PC settings and key map from the saves folder (kz1hd_pc.ini, kz1hd_keys.ini), and facts.txt (the port's version
and commit from manifest.json, Windows' version, the GPU the game's log names, what was shortened). Never included:
anything outside the install folder and the saves folder, the saves themselves, the game files, work/paths.env (the
folders your files are in). Only .log, .txt, .ini and .json names are read, and a game-file name is refused. Over CAP
bytes, the oldest lines of the largest logs go first. Zip name: project-vekta-support-YYYYmmdd-HHMMSS.zip.
Standard library only.
"""
import fnmatch
import glob
import json
import os
import platform
import re
import zipfile

CAP = 20 << 20          # bytes of text in the zip (uncompressed)
OK_EXT = (".log", ".txt", ".ini", ".json")
# game-file names, as assemble_payload.GAME_NAMES (the payload's scrub)
DENY = ("eboot.*", "param.sfo", "*.psarc", "*.elf", "*.self", "*.sprx", "*.prx", "*.edat", "*.sdat", "*.pkg", "*.rap",
        "*.pup", "*.trp", "*.pic")
GPU_RX = re.compile(r"using device \d+: ([^\n]+)")
MARKER = "[earlier lines removed to keep the package small]\n"


def refused(name):
    n = os.path.basename(name).lower()
    return any(fnmatch.fnmatch(n, p) for p in DENY) or not n.endswith(OK_EXT)


def zip_name(stamp):
    return f"project-vekta-support-{stamp}.zip"


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def candidates(root, data):
    """[(name in the zip, path)], in the order they are shown."""
    w = os.path.join(root, "work")
    out = [("play.log", os.path.join(w, "play.log"))]
    runs = sorted(glob.glob(os.path.join(w, "logs", "firstrun-*.log")), reverse=True)[:2]
    out += [("logs/" + os.path.basename(p), p) for p in runs]
    out += [("vekta-install.ini", os.path.join(root, "vekta-install.ini")),
            ("build-stamp.json", os.path.join(w, "build-stamp.json")),
            ("kz1hd_pc.ini", os.path.join(data, "kz1hd_pc.ini")),
            ("kz1hd_keys.ini", os.path.join(data, "kz1hd_keys.ini"))]
    return out


def inside(path, *folders):
    p = os.path.realpath(path)
    return any(p == os.path.realpath(f) or p.startswith(os.path.realpath(f) + os.sep) for f in folders)


def trim(texts, cap):
    """Drops the oldest lines of the largest entries until the total fits; the names of the shortened entries."""
    trimmed = []

    def size():
        return sum(len(t.encode("utf-8")) for t in texts.values())

    while size() > cap:
        n = max(texts, key=lambda k: len(texts[k].encode("utf-8")))
        t = texts[n]
        if len(t) <= len(MARKER):
            break
        cut = max(len(t) // 4, size() - cap)
        nl = t.find("\n", min(cut, len(t) - 1))
        texts[n] = MARKER + (t[nl + 1:] if nl >= 0 else "")
        if n not in trimmed:
            trimmed.append(n)
    return trimmed


def facts(root, texts, trimmed):
    try:
        with open(os.path.join(root, "manifest.json"), encoding="utf-8") as f:
            man = json.load(f)
    except (OSError, ValueError):
        man = {}
    gpu = next((m.group(1).strip()[:160] for t in texts.values() for m in [GPU_RX.search(t)] if m),
               "not in the logs")
    lines = ["Project Vekta support package",
             f"Port version: {man.get('version', 'unknown')}",
             f"Port commit: {man.get('commit') or 'unknown'}",
             f"Patch set: {man.get('patchset') or 'unknown'}",
             f"System: {platform.platform()}",
             f"GPU: {gpu}",
             ("Shortened (oldest lines removed): " + ", ".join(trimmed)) if trimmed else "Nothing was shortened.",
             "Files: " + ", ".join(sorted(texts))]
    return "\n".join(lines) + "\n"


def build(root, data, out_dir, red, stamp, cap=CAP):
    """(zip path, [what was skipped]); the path is "" when there was nothing to collect."""
    texts, skipped = {}, []
    for arc, path in candidates(root, data):
        if not os.path.isfile(path):
            continue
        if refused(arc) or refused(path) or not inside(path, root, data):
            skipped.append(f"refused {arc}")
            continue
        try:
            texts[arc] = red(read_text(path))
        except OSError:
            skipped.append(f"unreadable {arc}")
    if not texts:
        return "", skipped
    trimmed = trim(texts, cap - 4096)     # room for facts.txt
    texts["facts.txt"] = red(facts(root, texts, trimmed))
    os.makedirs(out_dir, exist_ok=True)
    dest = os.path.join(out_dir, zip_name(stamp))
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as z:
        for arc in ["facts.txt"] + [k for k in texts if k != "facts.txt"]:
            z.writestr(arc, texts[arc])
    return dest, skipped
