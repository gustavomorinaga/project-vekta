#!/usr/bin/env python3
"""The support package: one zip a player attaches to a bug report.

    launch.py --support-package [--out DIR]      (Start menu: "Make a support package"; scripts/support.cmd,
                                                  scripts/support.sh; default DIR: the Desktop)

After Project Helghast's tools/release/support.py (undecuple, MIT), from which the idea, the file list's shape and the
trimming come. It holds text only, all through firstrun.redactor() (the home folder becomes ~) and with the PC's
name taken out:
- the last game log (work/play.log) and the newest HISTORY earlier ones (work/logs/play-*.log), the newest two
  first-run logs (work/logs/firstrun-*.log), work/logs/sessions.jsonl (how each session ended: launch.py,
  sessions.py), vekta-install.ini, work/build-stamp.json, the PC settings and key map from the saves folder
  (kz1hd_pc.ini, kz1hd_keys.ini);
- the newest CRASH_FILES crash and hang reports the game wrote (work/crash/*.txt), and of each a copy with the
  names of the game's own frames, from the local build's symbol table (symbols.py: `func_0037DE70+0x40`);
- on Windows, the Application log's error reports for KZ1HD.exe (events 1000, 1001, 1002) and the System log's
  display driver events (event 4101, and the amdkmdag, nvlddmkm and igfx sources) of the last 24 hours, read with
  wevtutil, which needs no administrator (EVENTS events at most each); on Linux, the kernel log's GPU lines of the
  last 24 hours (journalctl -k, where the user may read it; skipped otherwise);
- report.txt, when the player typed a few words when asked;
- facts.txt: the port's version and commit from manifest.json, the system, the GPU and driver the game's log names
  (its [kz-sys] and [RSX vulkan] driver lines), how the last session ended, and which of the sources above are in
  the package.
Never included: anything outside the install folder and the saves folder (but the event and kernel log lines
above), the saves themselves, the game files, work/paths.env (the folders your files are in). Only .log, .txt, .ini,
.json and .jsonl names are read, and a game-file name is refused. Over CAP bytes, the oldest lines of the largest
logs go first. Zip name: project-vekta-support-YYYYmmdd-HHMMSS.zip. Nothing is sent anywhere: the player attaches the
zip. Standard library only.
"""
import fnmatch
import glob
import json
import os
import platform
import re
import socket
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import symbols  # noqa: E402

CAP = 20 << 20          # bytes of text in the zip (uncompressed)
OK_EXT = (".log", ".txt", ".ini", ".json", ".jsonl")
HISTORY = 3             # earlier game logs
CRASH_FILES = 10        # crash and hang reports
EVENTS = 50             # events per Windows log query
KERNEL_LINES = 200      # kernel log lines
# game-file names, as assemble_payload.GAME_NAMES (the payload's scrub)
DENY = ("eboot.*", "param.sfo", "*.psarc", "*.elf", "*.self", "*.sprx", "*.prx", "*.edat", "*.sdat", "*.pkg", "*.rap",
        "*.pup", "*.trp", "*.pic")
GPU_RX = re.compile(r"using device \d+: ([^\n]+)")
SYS_RX = re.compile(r"^\[kz-sys\] ([\w-]+): (.*)$", re.M)
DRIVER_RX = re.compile(r"^\[RSX vulkan\] driver: (.*)$", re.M)
VERSION_RX = re.compile(r"^(?:\[kz\] )?(Project Vekta \S+.*)$", re.M)
KERNEL_RX = re.compile(r"amdgpu|nouveau|nvidia|i915|\bxe\b|GPU hang|ring.*timeout", re.I)
MARKER = "[earlier lines removed to keep the package small]\n"
DAY_MS = 86400000
WIN_APP_QUERY = ("*[System[(EventID=1000 or EventID=1001 or EventID=1002) and "
                 f"TimeCreated[timediff(@SystemTime) <= {DAY_MS}]]]")
WIN_SYS_QUERY = ("*[System[(EventID=4101 or Provider[@Name='amdkmdag' or @Name='nvlddmkm' or @Name='igfx' or "
                 f"@Name='igfxn']) and TimeCreated[timediff(@SystemTime) <= {DAY_MS}]]]")


def refused(name):
    n = os.path.basename(name).lower()
    return any(fnmatch.fnmatch(n, p) for p in DENY) or not n.endswith(OK_EXT)


def zip_name(stamp):
    return f"project-vekta-support-{stamp}.zip"


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def crash_reports(root):
    """The newest CRASH_FILES work/crash/*.txt (a refused name never takes a place), newest first."""
    files = [p for p in glob.glob(os.path.join(root, "work", "crash", "*.txt")) if not refused(p)]
    return sorted(files, key=lambda p: (os.path.getmtime(p), p), reverse=True)[:CRASH_FILES]


def candidates(root, data):
    """[(name in the zip, path)], in the order they are shown."""
    w = os.path.join(root, "work")
    out = [("play.log", os.path.join(w, "play.log"))]
    hist = sorted(glob.glob(os.path.join(w, "logs", "play-*.log")), reverse=True)[:HISTORY]
    out += [("logs/" + os.path.basename(p), p) for p in hist]
    runs = sorted(glob.glob(os.path.join(w, "logs", "firstrun-*.log")), reverse=True)[:2]
    out += [("logs/" + os.path.basename(p), p) for p in runs]
    out.append(("logs/sessions.jsonl", os.path.join(w, "logs", "sessions.jsonl")))
    out += [("crash/" + os.path.basename(p), p) for p in crash_reports(root)]
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


def find_exe(root, environ=None):
    """The local build of the game (KZ_BIN, else work/build-win/KZ1HD.exe or work/build/KZ1HD), or ""."""
    environ = os.environ if environ is None else environ
    w = os.path.join(root, "work")
    for p in (environ.get("KZ_BIN", ""), os.path.join(w, "build-win", "KZ1HD.exe"), os.path.join(w, "build", "KZ1HD")):
        if p and os.path.isfile(p):
            return p
    return ""


def symbolized(texts, exe):
    """({name in the zip: report with names}, note for facts.txt) for each crash or hang report in texts."""
    reports = [n for n in texts if n.startswith("crash/") and os.path.basename(n).startswith(("crash-", "hang-"))]
    if not reports:
        return {}, ""
    table = symbols.load(exe) if exe else None
    if table is None or not len(table):
        why = "no local build" if not exe else ("no symbol table in " + os.path.basename(exe))
        return {}, f"Frame names: none ({why})."
    out, named = {}, 0
    for n in reports:
        text, k = symbols.symbolize(texts[n], table)
        named += k
        out[n[:-4] + ".symbolized.txt"] = text
    return out, f"Frame names: {named} frames named from {os.path.basename(exe)} ({table.kind}, {len(table)} symbols)."


def _run(argv, timeout=30):
    """A command's output as text, or None when it cannot run or fails."""
    try:
        r = subprocess.run(argv, capture_output=True, timeout=timeout,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0:
        return None
    enc = "oem" if os.name == "nt" else "utf-8"
    try:
        return r.stdout.decode(enc, "replace")
    except LookupError:
        return r.stdout.decode("utf-8", "replace")


def event_blocks(text, keep=None):
    """wevtutil /f:text's events ("Event[N]:" blocks), without the Computer and User lines; keep filters them."""
    blocks = [b for b in re.split(r"(?m)^(?=Event\[\d+\]:)", text or "") if b.strip()]
    out = []
    for b in blocks:
        if keep and not keep(b):
            continue
        out.append("\n".join(ln for ln in b.splitlines()
                             if not re.match(r"\s*(Computer|User)\s*:", ln)).rstrip() + "\n")
    return out


def windows_events(run=_run):
    """({name in the zip: text}, [facts lines]) from the Windows event logs (see the docstring)."""
    files, notes = {}, []
    app = run(["wevtutil", "qe", "Application", f"/q:{WIN_APP_QUERY}", "/f:text", f"/c:{EVENTS}", "/rd:true"])
    if app is None:
        notes.append("Windows Application log: could not be read")
    else:
        ev = event_blocks(app, lambda b: "kz1hd" in b.lower())
        notes.append(f"Windows Application log (last 24 h, error reports for KZ1HD.exe): {len(ev)} events")
        if ev:
            files["system/windows-application.txt"] = "\n".join(ev)
    sys_ = run(["wevtutil", "qe", "System", f"/q:{WIN_SYS_QUERY}", "/f:text", f"/c:{EVENTS}", "/rd:true"])
    if sys_ is None:
        notes.append("Windows System log: could not be read")
    else:
        ev = event_blocks(sys_)
        notes.append(f"Windows System log (last 24 h, display driver events): {len(ev)} events")
        if ev:
            files["system/windows-system.txt"] = "\n".join(ev)
    return files, notes


def linux_kernel(run=_run):
    """({name in the zip: text}, [facts lines]) from the kernel log's GPU lines of the last 24 hours."""
    text = run(["journalctl", "-k", "--since", "-24h", "--no-pager", "-q", "-o", "short-iso"], timeout=20)
    if text is None:
        return {}, ["Linux kernel log: not readable here (skipped)"]
    # short-iso lines are "<time> <host> kernel: ...": the host field goes (lines from before a rename, or boot lines
    # with the distribution's default name, would keep a host name the redaction does not know)
    lines = [re.sub(r"^(\S+) \S+ (kernel:)", r"\1 \2", ln) for ln in text.splitlines()
             if KERNEL_RX.search(ln)][-KERNEL_LINES:]
    note = [f"Linux kernel log (last 24 h, GPU lines): {len(lines)} lines"]
    return ({"system/linux-kernel.txt": "\n".join(lines) + "\n"} if lines else {}), note


def system_logs():
    """The system's own logs for this platform (see the docstring)."""
    if os.name == "nt":
        return windows_events()
    if sys.platform.startswith("linux"):
        return linux_kernel()
    return {}, []


def manifest(root):
    try:
        with open(os.path.join(root, "manifest.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def last_session(root):
    path = os.path.join(root, "work", "logs", "sessions.jsonl")
    last = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                if isinstance(d, dict) and d.get("end"):
                    last = d
    except OSError:
        pass
    return last


def summary(root, log_text=None):
    """What the bug form and facts.txt show: {version, system, gpu, driver, started_by, sys (the [kz-sys] lines),
    session (the last ended one), signature}."""
    if log_text is None:
        try:
            log_text = read_text(os.path.join(root, "work", "play.log"))
        except OSError:
            log_text = ""
    man = manifest(root)
    kz = {}
    for k, v in SYS_RX.findall(log_text):
        kz.setdefault(k, []).append(v.strip())
    drv = DRIVER_RX.search(log_text)
    gpu = GPU_RX.search(log_text)
    sess = last_session(root)
    sig = re.search(r"\[kz-(?:crash|hang)\] signature: (KZ-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4})", log_text)
    version = man.get("version") or ""
    if not version:
        try:
            version = read_text(os.path.join(root, "VERSION")).strip()
        except OSError:
            m = VERSION_RX.search(log_text)
            version = m.group(1) if m else "unknown"
    patchset = man.get("patchset") or ""
    if not patchset:   # a source tree has no manifest: the game's version line names it
        m = re.search(r"\(patch set ([0-9a-f]{8,64})[,)]", log_text)
        patchset = m.group(1) if m else ""
    return {"version": version, "commit": man.get("commit") or "", "patchset": patchset,
            "system": (kz.get("os") or [platform.platform()])[0],
            "gpu": ((kz.get("gpu") or [""])[0] or (gpu.group(1).strip() if gpu else ""))[:160],
            "driver": ((kz.get("driver") or [""])[0] or (drv.group(1).strip() if drv else ""))[:160],
            "started_by": (kz.get("started-by") or [os.environ.get("KZ_STARTED_BY", "")])[0],
            "sys": kz, "session": sess,
            "signature": sig.group(1) if sig else sess.get("signature", "")}


def facts(root, texts, trimmed, notes=()):
    s = summary(root, texts.get("play.log", ""))
    sess = s["session"]
    lines = ["Project Vekta support package",
             f"Port version: {s['version']}",
             f"Port commit: {s['commit'] or 'unknown'}",
             f"Patch set: {s['patchset'] or 'unknown'}",
             f"System: {platform.platform()}",
             f"GPU: {s['gpu'] or 'not in the logs'}",
             f"Driver: {s['driver'] or 'not in the logs'}",
             f"Started by: {s['started_by'] or 'unknown'}"]
    if sess:
        lines.append(f"Last session: exit {sess.get('exit_text', sess.get('exit'))} ({sess.get('meaning', '')}) after "
                     f"{sess.get('seconds', '?')} s, {sess.get('kind', '?')}, ended {sess.get('end', '?')}")
    if s["signature"]:
        lines.append(f"Crash signature: {s['signature']}")
    if s["sys"]:
        lines.append("What the game's log says about this PC:")
        lines += [f"  {k}: {v}" for k, vs in s["sys"].items() for v in vs]
    lines.append("Sources included:")
    lines += [f"  {n}" for n in notes]
    lines += [("Shortened (oldest lines removed): " + ", ".join(trimmed)) if trimmed else "Nothing was shortened.",
              "Files: " + ", ".join(sorted(texts))]
    return "\n".join(lines) + "\n"


def sources(texts):
    """facts.txt's lines on the files read from the install and saves folders."""
    n = sorted(texts)
    hist = [k for k in n if k.startswith("logs/play-")]
    runs = [k for k in n if k.startswith("logs/firstrun-")]
    crash = [k for k in n if k.startswith("crash/")]
    out = ["play.log (the last session)" if "play.log" in n else "play.log: none"]
    out.append(f"earlier game logs: {len(hist)}")
    out.append(f"first-run logs: {len(runs)}")
    out.append("sessions.jsonl" if "logs/sessions.jsonl" in n else "sessions.jsonl: none")
    out.append(f"crash and hang reports (work/crash): {len(crash)}")
    out += [k for k in n if "/" not in k and k != "play.log"]
    return out


def build(root, data, out_dir, red, stamp, cap=CAP, note="", system=None, exe=None):
    """(zip path, [what was skipped]); the path is "" when there was nothing to collect. note: the player's words
    (report.txt); system: () -> ({name: text}, [facts lines]) for the system's logs (default system_logs);
    exe: the build to name frames from (default find_exe)."""
    texts, skipped = {}, []
    host = socket.gethostname()
    host_rx = re.compile(r"(?<![\w-])" + re.escape(host) + r"(?![\w-])", re.I) if len(host) >= 4 else None

    def clean(s):
        s = red(s)
        return host_rx.sub("<this PC>", s) if host_rx else s
    for arc, path in candidates(root, data):
        if not os.path.isfile(path):
            continue
        if refused(arc) or refused(path) or not inside(path, root, data):
            skipped.append(f"refused {arc}")
            continue
        try:
            texts[arc] = clean(read_text(path))
        except OSError:
            skipped.append(f"unreadable {arc}")
    if not texts:
        return "", skipped
    notes = sources(texts)
    sym, sym_note = symbolized(texts, find_exe(root) if exe is None else exe)
    texts.update(sym)
    if sym_note:
        notes.append(sym_note)
    try:
        more, more_notes = (system or system_logs)()
    except Exception as err:     # a system log is a bonus: never the reason a package fails
        more, more_notes = {}, [f"system logs: skipped ({type(err).__name__})"]
    for arc, text in more.items():
        if not refused(arc):
            texts[arc] = clean(text)
    notes += more_notes
    if note.strip():
        texts["report.txt"] = clean(note.strip() + "\n")
        notes.append("report.txt: what you typed")
    trimmed = trim(texts, cap - 8192)     # room for facts.txt
    texts["facts.txt"] = clean(facts(root, texts, trimmed, notes))
    os.makedirs(out_dir, exist_ok=True)
    dest = os.path.join(out_dir, zip_name(stamp))
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as z:
        for arc in ["facts.txt"] + [k for k in texts if k != "facts.txt"]:
            z.writestr(arc, texts[arc])
    return dest, skipped
