#!/usr/bin/env python3
"""What the launcher keeps of each game session (tools/release/launch.py): how it ended, the logs of the last ones,
and the reports the game left. Standard library only.

- The end of a session: one line appended to work/play.log,
  `[launcher] session ended: exit <code> (<meaning>) after <N> s, <kind>`, and one JSON line in
  work/logs/sessions.jsonl (start and end, ISO times; seconds; exit code; kind; version; started-by; the crash and
  hang report files the session left in work/crash; the crash signature from the log). The line is written when the
  game starts (start only) and completed when it ends, so a session that never ended (the PC lost power, the
  launcher was stopped) is the last line, with no end: the next start says so once, and marks it "interrupted".
- Kinds: clean (exit 0, no quit line), quit (the game's "[kz] quitting (...)" line, or the window closed: the
  player ended it), crash (a crash report, a Windows crash code, or a POSIX crash signal), hang-killed (a hang report:
  the game's watchdog stopped it), gpu-lost (exit 78, KZ_EXIT_GPU_LOST, or "[RSX vulkan] device lost"), killed
  (terminated from outside: 0x40010004, Ctrl+C, SIGKILL, SIGTERM), failed (any other code). The line format and the
  kinds are shared with the game's crash reporter (port/kz1hd): see docs/TROUBLESHOOTING.md.
- The log history: before a session, the last play.log moves to work/logs/play-YYYYmmdd-HHMMSS.log (its session's
  start). Kept: the KEEP newest, and the KEEP_BAD newest of those whose session ended in a crash, a hang or a lost
  GPU (sessions.jsonl says which). play.log keeps its name: the docs and recomplauncher.json point at it.
- After a session play.log is rewritten through firstrun.redactor() (the home folder becomes ~), and each run of
  identical lines becomes the line once with ` [repeated N times]` (the logs of #129 were three quarters one
  repeated line).
"""
import datetime
import glob
import json
import os
import re
import signal

KEEP, KEEP_BAD = 5, 3
BAD = ("crash", "hang-killed", "gpu-lost")
ABNORMAL = BAD + ("killed", "failed")
JOURNAL = "sessions.jsonl"
JOURNAL_MAX = 200          # sessions kept in sessions.jsonl
QUIT_MARK = b"[kz] quitting ("
CLOSED_MARKS = (b"window closed", b"window disabled", QUIT_MARK)
GPU_LOST_FALLBACK = 78     # port/kz1hd/kz_exit.h KZ_EXIT_GPU_LOST, when this tree's header does not have it yet
# Windows' NTSTATUS codes a process ends with: (meaning, kind)
NTSTATUS = {
    0xC0000005: ("access violation", "crash"),
    0xC0000409: ("stack buffer overrun: the program stopped itself (fail fast)", "crash"),
    0xC00000FD: ("stack overflow", "crash"),
    0xC000001D: ("illegal instruction", "crash"),
    0xC0000094: ("integer division by zero", "crash"),
    0xC0000374: ("heap corruption", "crash"),
    0xC0000135: ("a DLL was not found", "failed"),
    0xC0000142: ("a DLL could not start", "failed"),
    0x40010004: ("killed: the process was terminated", "killed"),
    0xC000013A: ("stopped with Ctrl+C or its console closed", "killed"),
}
CRASH_SIGNALS = ("SIGSEGV", "SIGBUS", "SIGILL", "SIGFPE", "SIGABRT", "SIGTRAP", "SIGSYS")
KILL_SIGNALS = ("SIGKILL", "SIGTERM", "SIGINT", "SIGHUP", "SIGQUIT")
SIGNAL_TEXT = {"SIGSEGV": "segmentation fault", "SIGBUS": "bus error", "SIGILL": "illegal instruction",
               "SIGFPE": "arithmetic error", "SIGABRT": "aborted", "SIGTRAP": "trap", "SIGSYS": "bad system call",
               "SIGKILL": "killed", "SIGTERM": "terminated", "SIGINT": "interrupted", "SIGHUP": "hang-up",
               "SIGQUIT": "quit"}
WORDS = {"gpu": "GPU", "cpu": "CPU", "eboot": "EBOOT", "rpcs3": "RPCS3", "vulkan": "Vulkan"}
SIGNATURE_RX = re.compile(r"\[kz-(?:crash|hang)\] signature: (KZ-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4})")


def gpu_lost_code(codes):
    return codes.get("GPU_LOST", (GPU_LOST_FALLBACK, ""))[0]


def signal_name(n):
    try:
        return signal.Signals(n).name
    except ValueError:
        return f"signal {n}"


def exit_text(rc):
    """The code as the line shows it: 0xC0000005 for an NTSTATUS, -11 for a POSIX signal, else the number."""
    if rc >= 0x80000000 or rc in NTSTATUS:
        return f"0x{rc & 0xFFFFFFFF:08X}"
    return str(rc)


def meaning(rc, codes):
    """A few words on what the code means. codes: launch.EXIT, {name: (code, text)} from kz_exit.h."""
    if rc == 0:
        return "ok"
    if rc in NTSTATUS:
        return NTSTATUS[rc][0]
    if rc < 0:
        name = signal_name(-rc)
        return f"signal {-rc}, {name}" + (f": {SIGNAL_TEXT[name]}" if name in SIGNAL_TEXT else "")
    names = {code: name for name, (code, _) in codes.items()}
    names.setdefault(GPU_LOST_FALLBACK, "GPU_LOST")
    if rc in names:
        return " ".join(WORDS.get(w, w) for w in names[rc].lower().split("_"))
    if rc >= 0x80000000:
        return "a Windows error code"
    return "an error"


def scan(log):
    """What the log says about the session's end: {quit, closed, crash, hang, gpu_lost, signature}."""
    try:
        with open(log, "rb") as f:
            data = f.read()
    except OSError:
        data = b""
    text = data.decode("utf-8", "replace")
    m = SIGNATURE_RX.search(text)
    return {"quit": QUIT_MARK in data, "closed": any(c in data for c in CLOSED_MARKS),
            "crash": b"[kz-crash] begin" in data, "hang": b"[kz-hang] begin" in data,
            "gpu_lost": b"[RSX vulkan] device lost" in data, "signature": m.group(1) if m else ""}


def classify(rc, seen, codes, reports=()):
    """The session's kind (see the docstring). seen: scan()'s; reports: the report file names it left."""
    if seen.get("hang") or any(r.startswith("hang-") for r in reports):
        return "hang-killed"
    if rc == gpu_lost_code(codes) or seen.get("gpu_lost"):
        return "gpu-lost"
    sig = signal_name(-rc) if rc < 0 else ""
    if seen.get("crash") or any(r.startswith("crash-") for r in reports) or sig in CRASH_SIGNALS or \
            NTSTATUS.get(rc, ("", ""))[1] == "crash":
        return "crash"
    if seen.get("closed"):
        return "quit"
    if rc == 0:
        return "clean"
    if sig in KILL_SIGNALS or NTSTATUS.get(rc, ("", ""))[1] == "killed":
        return "killed"
    return "failed"


def end_line(rc, seconds, kind, codes):
    return f"[launcher] session ended: exit {exit_text(rc)} ({meaning(rc, codes)}) after {int(seconds)} s, {kind}"


def now():
    return datetime.datetime.now().astimezone()


def iso(t):
    return t.isoformat(timespec="seconds")


def stamp(t):
    return t.strftime("%Y%m%d-%H%M%S")


def journal_path(work):
    return os.path.join(work, "logs", JOURNAL)


def read_journal(work):
    """[session dict], oldest first; lines that are not JSON objects are skipped."""
    out = []
    try:
        with open(journal_path(work), encoding="utf-8", errors="replace") as f:
            for line in f:
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                if isinstance(d, dict):
                    out.append(d)
    except OSError:
        pass
    return out


def write_journal(work, sessions):
    path = journal_path(work)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        for s in sessions[-JOURNAL_MAX:]:
            f.write(json.dumps(s, sort_keys=True) + "\n")
    os.replace(tmp, path)


def unfinished(work):
    """The last session when it started and never ended (and was not already marked), else None."""
    s = read_journal(work)
    if s and s[-1].get("start") and not s[-1].get("end") and s[-1].get("kind") != "interrupted":
        return s[-1]
    return None


def mark_interrupted(work, log):
    """The unfinished last session: marked in sessions.jsonl, and a line saying so appended to its play.log."""
    s = read_journal(work)
    if not s:
        return
    s[-1]["kind"] = "interrupted"
    write_journal(work, s)
    if os.path.isfile(log):
        with open(log, "a", encoding="utf-8", newline="\n") as f:
            f.write("[launcher] session ended: not seen (the PC or the launcher stopped first), interrupted\n")


def history_name(t):
    return f"play-{stamp(t)}.log"


def rotate(work, log):
    """Moves the last play.log into work/logs as play-<its session's start>.log; returns the new name or ""."""
    try:
        if os.path.getsize(log) == 0:
            return ""
        mtime = os.path.getmtime(log)
    except OSError:
        return ""
    s = read_journal(work)
    name = ""
    if s and s[-1].get("log") and s[-1].get("start"):
        try:
            started = datetime.datetime.fromisoformat(s[-1]["start"]).timestamp()
        except ValueError:
            started = None
        if started is not None and started <= mtime + 2:
            name = s[-1]["log"]
    if not name:
        name = history_name(datetime.datetime.fromtimestamp(mtime))
    logs = os.path.join(work, "logs")
    os.makedirs(logs, exist_ok=True)
    dest = os.path.join(logs, name)
    n = 1
    while os.path.exists(dest):
        n += 1
        dest = os.path.join(logs, name[:-4] + f"-{n}.log")
    os.replace(log, dest)
    return os.path.basename(dest)


def history(work):
    """The history logs, newest first."""
    return sorted((os.path.basename(p) for p in glob.glob(os.path.join(work, "logs", "play-*.log"))), reverse=True)


def prune(work):
    """Keeps the KEEP newest history logs and the KEEP_BAD newest of a crash, hang or lost GPU; returns the removed."""
    kinds = {s.get("log"): s.get("kind") for s in read_journal(work) if s.get("log")}
    names = history(work)
    keep = set(names[:KEEP]) | set([n for n in names if kinds.get(n) in BAD][:KEEP_BAD])
    gone = []
    for n in names:
        if n not in keep:
            try:
                os.remove(os.path.join(work, "logs", n))
                gone.append(n)
            except OSError:
                pass
    return gone


def begin(work, version, started_by, t=None):
    """The session's start: its line in sessions.jsonl (completed by finish()); returns the session dict."""
    t = t or now()
    sess = {"start": iso(t), "log": history_name(t), "version": version, "started_by": started_by}
    s = read_journal(work)
    s.append(sess)
    write_journal(work, s)
    return sess


def report_files(work):
    """{name: mtime} of the crash and hang reports in work/crash."""
    out = {}
    for p in glob.glob(os.path.join(work, "crash", "*.txt")):
        n = os.path.basename(p)
        try:
            if n.startswith(("crash-", "hang-")):
                out[n] = os.path.getmtime(p)
        except OSError:
            pass
    return out


def reports_since(work, before):
    """The crash and hang reports the session left: the names report_files() did not list before it (or listed with
    an older time: a report written again in the same second), oldest first."""
    return sorted(n for n, t in report_files(work).items() if n not in before or t > before[n])


def finish(work, sess, rc, seconds, kind, reports, signature, codes, t=None):
    """Completes the session's line in sessions.jsonl (the last one with its start)."""
    t = t or now()
    sess.update({"end": iso(t), "seconds": int(seconds), "exit": rc, "exit_text": exit_text(rc),
                 "meaning": meaning(rc, codes), "kind": kind, "reports": list(reports)})
    if signature:
        sess["signature"] = signature
    s = read_journal(work)
    for i in range(len(s) - 1, -1, -1):
        if s[i].get("start") == sess["start"]:
            s[i] = sess
            break
    else:
        s.append(sess)
    write_journal(work, s)
    return sess


def tidy(log, redact, extra=""):
    """Rewrites the log through redact, each run of identical lines as one line with ` [repeated N times]`, then
    appends extra (a line); returns (lines in, lines out)."""
    if not os.path.isfile(log):
        if extra:
            with open(log, "w", encoding="utf-8", newline="\n") as f:
                f.write(extra + "\n")
        return 0, 0
    tmp = log + ".tidy"
    n_in = n_out = 0
    with open(log, "rb") as src, open(tmp, "w", encoding="utf-8", newline="\n") as dst:
        prev, count = None, 0

        def flush():
            nonlocal n_out
            if prev is not None:
                dst.write(prev + (f" [repeated {count} times]" if count > 1 else "") + "\n")
                n_out += 1
        for raw in src:
            n_in += 1
            line = redact(raw.decode("utf-8", "replace").rstrip("\r\n"))
            if line == prev:
                count += 1
                continue
            flush()
            prev, count = line, 1
        flush()
        if extra:
            dst.write(extra + "\n")
    os.replace(tmp, log)
    return n_in, n_out
