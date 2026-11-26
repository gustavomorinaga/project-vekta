#!/usr/bin/env python3
"""Host test for the player-side reporting (tools/release/launch.py, sessions.py, support.py, symbols.py): a stand-in
game (a shell script) that ends each way gives the session line in play.log and the line in work/logs/sessions.jsonl,
with the right kind, meaning, crash reports and signature; the Windows crash codes and POSIX signals are named; the
log history keeps the five newest and the three newest crashes; play.log is rewritten with the home folder as ~ and
repeated lines folded; a session that never ended is said once; an abnormal end offers the support package and a
quit does not; the package holds the crash reports, the earlier logs, sessions.jsonl, frame names from a PE (made
here byte by byte) and an ELF (built here with cc), no home path and no game-file name; the bug form's link fills
the template's fields; KZ_CRASH_DIR and KZ_STARTED_BY reach the game. No game data, no network."""
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
sys.path.insert(0, str(ROOT / "tools"))
import firstrun  # noqa: E402
import launch  # noqa: E402
import sessions  # noqa: E402
import support  # noqa: E402
import symbols  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


def make_root(t):
    root = t / "app"
    w = root / "work"
    usr = w / "vfs" / "PS3_GAME" / "USRDIR"
    usr.mkdir(parents=True)
    (usr / "EBOOT.elf").write_bytes(b"stand-in")
    (w / "build").mkdir()
    (root / "VERSION").write_text("9.8.7\n")
    return root, w


def game(exe, body):
    exe.write_text("#!/bin/sh\n" + body + "\n")
    exe.chmod(0o755)


# The codes and their meanings
E = launch.EXIT
check(sessions.end_line(0xC0000005, 72.4, "crash", E) ==
      "[launcher] session ended: exit 0xC0000005 (access violation) after 72 s, crash",
      "the contract's line for an access violation")
for code, kind in ((0xC0000005, "crash"), (0xC0000409, "crash"), (0xC00000FD, "crash"), (0xC000001D, "crash"),
                   (0xC0000094, "crash"), (0xC0000374, "crash"), (0xC0000135, "failed"), (0xC0000142, "failed"),
                   (0x40010004, "killed")):
    check(sessions.classify(code, {}, E) == kind and sessions.meaning(code, E) != "an error" and
          sessions.exit_text(code) == f"0x{code:08X}", f"0x{code:08X}: {sessions.meaning(code, E)}, {kind}")
check(sessions.meaning(-11, E) == "signal 11, SIGSEGV: segmentation fault" and sessions.classify(-11, {}, E) == "crash",
      f"a POSIX signal is named: {sessions.meaning(-11, E)}")
check(sessions.classify(-9, {}, E) == "killed" and sessions.classify(-15, {}, E) == "killed", "SIGKILL, SIGTERM: killed")
check(sessions.meaning(78, {}) == "GPU lost" and sessions.classify(78, {}, {}) == "gpu-lost",
      "78 is the lost GPU even before kz_exit.h has KZ_EXIT_GPU_LOST")
check(sessions.meaning(E["CPU"][0], E) == "CPU", f"kz_exit.h's names: {sessions.meaning(E['CPU'][0], E)}")
check(sessions.classify(-9, {"closed": True}, E) == "quit" and sessions.classify(1, {"closed": True}, E) == "quit",
      "the window closed (the launcher stops the game then): the player quit")
check(sessions.classify(0, {"closed": True, "crash": True}, E) == "crash", "a crash report wins over a quit line")

with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    root, w = make_root(t)
    exe = w / "build" / "KZ1HD"
    game(exe, "exit 0")
    env0 = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(t)}
    argv, cwd, env, log = launch.plan(str(root), env0)
    check(env["KZ_CRASH_DIR"] == str(w / "crash") and env["KZ_STARTED_BY"] == "zip",
          f"the game is told KZ_CRASH_DIR and KZ_STARTED_BY: {env['KZ_CRASH_DIR']} {env['KZ_STARTED_BY']}")
    check(launch.plan(str(root), dict(env0, RECOMPLAUNCHER="1"))[2]["KZ_STARTED_BY"] == "recomplauncher" and
          launch.started_by(str(root), {"QUIVER_APP": "x"}) == "quiver" and
          launch.plan(str(root), dict(env0, KZ_STARTED_BY="source"))[2]["KZ_STARTED_BY"] == "source",
          "RecompLauncher's and Quiver's variables are recognised, and a KZ_STARTED_BY given wins")
    (root / ".git").write_text("gitdir: x\n")
    check(launch.started_by(str(root), {}) == "source", "a Git checkout: source")
    (root / launch.INSTALL_INI).write_text("[install]\nportable=1\n")
    check(launch.started_by(str(root), {}) == "setup", "the setup program's install: setup")
    (root / launch.INSTALL_INI).unlink()
    (root / ".git").unlink()

    crash_block = "\n".join(f"[kz-crash] {x}" for x in (
        "begin", "signal: 11 SIGSEGV", "address: 0x0", "thread: 7 main", "uptime: 1 s", "module: KZ1HD+0x1234",
        "#0 KZ1HD+0x1234", "#1 libc.so.6+0x29d90", "signature: KZ-1A2B-3C4D", "end"))
    kinds = {
        "clean": "exit 0",
        "quit": 'echo "[kz] quitting (Quit Game)"\nexit 0',
        "crash": (f"cat >&2 <<'EOF'\n{crash_block}\nEOF\n"
                  'printf "begin\\n#0 KZ1HD+0x1234\\nend\\n" > "$KZ_CRASH_DIR/crash-20261123-101010.txt"\n'
                  "kill -SEGV $$"),
        "hang-killed": ('echo "[kz-hang] begin"; echo "[kz-hang] no new frame for 10 s"\n'
                        'echo "begin" > "$KZ_CRASH_DIR/hang-20261123-101011.txt"\nkill -KILL $$'),
        "gpu-lost": 'echo "[RSX vulkan] device lost in vkQueueSubmit"\nexit 78',
        "killed": "kill -TERM $$",
        "failed": 'echo "ERROR: something"\nexit 3',
    }
    for kind, body in kinds.items():
        game(exe, body)
        rc, rec = launch.play(str(root), argv, cwd, env, log)
        last = pathlib.Path(log).read_text().splitlines()[-1]
        j = sessions.read_journal(str(w))[-1]
        want = sessions.end_line(rc, rec["seconds"], kind, E)
        check(rec["kind"] == kind and last == want, f"{kind}: {last}")
        check(j["kind"] == kind and j["exit"] == rc and j["start"] and j["end"] and "seconds" in j and
              j["version"] == "9.8.7" and j["started_by"] == "zip", f"{kind}: sessions.jsonl {json.dumps(j)}")
    j = sessions.read_journal(str(w))
    crash = next(s for s in j if s["kind"] == "crash")
    hang = next(s for s in j if s["kind"] == "hang-killed")
    check(crash["reports"] == ["crash-20261123-101010.txt"] and crash["signature"] == "KZ-1A2B-3C4D" and
          crash["exit"] == -11 and "SIGSEGV" in crash["meaning"],
          f"the crash: its report file, its signature, the signal: {crash}")
    check(hang["reports"] == ["hang-20261123-101011.txt"], f"the hang's report file: {hang['reports']}")
    check(len(j) == len(kinds) and all(s.get("end") for s in j), "one complete line per session")
    hist = sessions.history(str(w))
    check(len(hist) == sessions.KEEP and
          not any(n == "play.log" for n in hist), f"each earlier play.log moved to work/logs: {hist}")
    check("[launcher] session ended" in (w / "logs" / hist[0]).read_text(), "an earlier log keeps its end line")

    # A hang the game recovered from ("frames again" after it) and then quit normally is a quit, not a hang (the
    # launcher used to say "exit 0: ok (hang-killed)"); still stuck at the end, closed or killed, it is one.
    recovered = ('echo "[kz-hang] begin"; echo "[kz-hang] end"\n'
                 'echo "begin" > "$KZ_CRASH_DIR/hang-20261123-101012.txt"\n'
                 'echo "[kz] hang watch: frames again after 6.2 s"\necho "[kz] quitting (Quit Game)"\nexit 0')
    for body, kind in ((recovered, "quit"),
                       (recovered.replace('echo "[kz] quitting (Quit Game)"\nexit 0',
                                          'echo "[kz-hang] begin"\necho "[kz] quitting (signal)"\nexit 0'),
                        "hang-killed")):
        game(exe, body)
        rc, rec = launch.play(str(root), argv, cwd, env, log)
        check(rec["kind"] == kind, f"a hang, frames again, then {kind}: got {rec['kind']}")
    check(sessions.classify(0, {}, E, ["hang-20261123-101011.txt"]) == "hang-killed",
          "a hang report file with no log to read is still a hang")
    check(sessions.classify(0, {"hang_over": True, "closed": True}, E, ["hang-20261123-101011.txt"]) == "quit",
          "a hang report file the log shows the game recovered from is not")

    # The messages: an abnormal end offers the package; a quit, nothing
    said, asked, made = [], [], []
    real = {k: getattr(launch, k) for k in ("notify", "ask", "can_ask", "support_package", "vulkan_present")}
    launch.notify = lambda text, error=True: said.append(text)
    launch.ask = lambda text: asked.append(text) or True
    launch.can_ask = lambda: True
    launch.support_package = lambda root, out="", environ=None, note=None: made.append(root) or 0
    launch.vulkan_present = lambda: True
    old_env = dict(os.environ)
    os.environ.update(HOME=str(t))
    try:
        game(exe, f"cat >&2 <<'EOF'\n{crash_block}\nEOF\nsleep 0.1\nkill -SEGV $$")
        rc = launch.main(["--root", str(root)])
        check(rc == -11 and asked and "The game closed unexpectedly" in asked[-1] and "SIGSEGV" in asked[-1] and
              "Signature: KZ-1A2B-3C4D" in asked[-1] and "Make a support package now?" in asked[-1] and made,
              f"a crash: the message, the meaning, the signature, the offer, and yes makes the package: {asked[-1:]}")
        asked.clear(), made.clear()
        game(exe, 'echo "[kz] quitting (window closed)"\nexit 0')
        launch.main(["--root", str(root)])
        check(not asked and not made and not said, f"a quit: no message at all: {asked} {said}")
        game(exe, 'echo "ERROR: could not load the EBOOT"\nexit 1')
        launch.main(["--root", str(root)])
        check(asked and "stopped right after it started (exit code 1)" in asked[-1] and
              "could not load the EBOOT" in asked[-1] and "support package" in asked[-1],
              f"a start failure keeps its early-exit message, with the offer: {asked[-1:]}")
        asked.clear()
        game(exe, "sleep 0.1\nexit 3")
        real_early = launch.EARLY_EXIT
        launch.EARLY_EXIT = 0
        try:
            launch.main(["--root", str(root)])
        finally:
            launch.EARLY_EXIT = real_early
        check(asked and "The game closed unexpectedly" in asked[-1] and "exit 3" in asked[-1].lower(),
              f"an error code after the first seconds is abnormal too: {asked[-1:]}")
        # A session that started and never ended: said once, then marked
        asked.clear()
        sessions.begin(str(w), "9.8.7", "zip")
        (w / "play.log").write_text("[kz] Project Vekta 9.8.7\n[rsx] frame\n")
        check(launch.check_unfinished(str(root), str(w), str(w / "play.log")) and asked and
              "did not end normally" in asked[-1], f"the next start says so: {asked[-1:]}")
        check(not launch.check_unfinished(str(root), str(w), str(w / "play.log")),
              "and only once (marked interrupted)")
        check("interrupted" in (w / "play.log").read_text(), "its log says the end was not seen")
        launch.can_ask = lambda: False
        said.clear(), asked.clear()
        game(exe, "kill -SEGV $$")
        launch.main(["--root", str(root)])
        check(not asked and said and "support.sh" in said[-1],
              f"no terminal (a launcher's pipe): the text and how to make the package, no question: {said[-1:]}")
    finally:
        for k, v in real.items():
            setattr(launch, k, v)
        os.environ.clear()
        os.environ.update(old_env)

# History retention: the five newest, and the three newest crashes, hangs or lost GPUs
with tempfile.TemporaryDirectory() as tmp:
    w = pathlib.Path(tmp) / "work"
    (w / "logs").mkdir(parents=True)
    names = [f"play-202611{d:02d}-120000.log" for d in range(1, 16)]
    bad = {2: "crash", 4: "hang-killed", 6: "gpu-lost", 8: "crash", 13: "crash"}
    recs = []
    for i, n in enumerate(names, 1):
        (w / "logs" / n).write_text(n)
        recs.append({"start": f"2026-11-{i:02d}T12:00:00", "end": "x", "log": n, "kind": bad.get(i, "quit")})
    sessions.write_journal(str(w), recs)
    gone = sessions.prune(str(w))
    left = sorted(sessions.history(str(w)))
    want = sorted(names[10:] + [names[7], names[5]])   # 11-15, and of the three newest bad (13, 8, 6) 8 and 6
    check(left == want and len(gone) == len(names) - len(want), f"5 newest + 3 newest bad: {left}")
    # rotate: the last play.log takes its session's start as its name
    log = w / "play.log"
    s = sessions.begin(str(w), "1", "zip")
    log.write_text("x\n")
    check(sessions.rotate(str(w), str(log)) == s["log"] and not log.exists(), f"play.log moves to {s['log']}")
    log.write_text("")
    check(sessions.rotate(str(w), str(log)) == "", "an empty log stays")

# The log rewritten: the home folder as ~, a run of one line folded
with tempfile.TemporaryDirectory() as tmp:
    home = tmp
    log = pathlib.Path(tmp) / "play.log"
    spurs = "[cellSpurs] _SendSignal(0x1, 3)"
    log.write_text(f"[kz] Project Vekta 9.8.7\n[vfs] {home}/x/y\n" + (spurs + "\n") * 120 + "[rsx] a\n[rsx] a\n"
                   "[rsx] b\n")
    n_in, n_out = sessions.tidy(str(log), firstrun.redactor(home=home, windows=False), "[launcher] end")
    lines = log.read_text().splitlines()
    check(lines == ["[kz] Project Vekta 9.8.7", "[vfs] ~/x/y", spurs + " [repeated 120 times]",
                    "[rsx] a [repeated 2 times]", "[rsx] b", "[launcher] end"] and n_in == 125 and n_out == 5,
          f"redacted and folded: {lines}")
    check(launch.window_closed(str(log)) is False, "the folded log is still read by window_closed")

# Frame names: a PE with a COFF symbol table, made here
with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    long_name = b"_Z13func_0037DE70P11ppu_context"
    strtab = struct.pack("<I", 4 + len(long_name) + 1) + long_name + b"\0"
    syms = [struct.pack("<8sIhHBB", b".text", 0, 1, 0, 3, 1), b"\0" * 18,                # section symbol + aux
            struct.pack("<8sIhHBB", b"\0\0\0\0" + struct.pack("<I", 4), 0x40, 1, 0x20, 2, 0),
            struct.pack("<8sIhHBB", b"helper", 0x100, 1, 0x20, 3, 0),
            struct.pack("<8sIhHBB", b"absval", 0x10, -1, 0, 2, 0)]
    symptr = 0x400
    pe = bytearray(0x400)
    pe[0:2] = b"MZ"
    struct.pack_into("<I", pe, 0x3C, 0x40)
    pe[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", pe, 0x44, 0x8664, 1, 0, symptr, len(syms), 0, 0x22)
    struct.pack_into("<8sIIIIIIHHI", pe, 0x58, b".text", 0x200, 0x1000, 0x200, 0x200, 0, 0, 0, 0, 0x60000020)
    pe += b"".join(syms) + strtab
    (t / "KZ1HD.exe").write_bytes(bytes(pe))
    table = symbols.load(str(t / "KZ1HD.exe"))
    text = ("[kz-crash] #0 KZ1HD.exe+0x1050\n[kz-crash] #1 KZ1HD.exe+0x1104\n#2 KZ1HD.exe+0x5000\n"
            "#3 ntdll.dll+0x1050\nmodule: kz1hd.exe+0x1040\n")
    out, named = symbols.symbolize(text, table)
    check(table.kind == "pe" and len(table) == 2 and named == 3 and out.splitlines() == [
        "[kz-crash] #0 KZ1HD.exe+0x1050  func_0037DE70+0x10", "[kz-crash] #1 KZ1HD.exe+0x1104  helper+0x4",
        "#2 KZ1HD.exe+0x5000", "#3 ntdll.dll+0x1050", "module: kz1hd.exe+0x1040  func_0037DE70+0x0"],
        f"PE: the lifted name demangled, a static function, a frame past the section and another module as they are: "
        f"{out!r}")
    check(symbols.demangle("_Z13func_0037DE70P11ppu_context") == "func_0037DE70" and
          symbols.demangle("_ZN3foo3barEv") == "_ZN3foo3barEv" and symbols.demangle("main") == "main",
          "only the simple lifted pattern is demangled")
    check(symbols.load(str(t / "nothing")) is None and symbols.load(str(ROOT / "VERSION")) is None,
          "no file or not an executable: no table")

# The support package: everything the report needs, nothing it must not hold
cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
check(cc is not None, "a C compiler is there to build the ELF stand-in")
with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    home = str(t)
    root, w = make_root(t)
    data = w / "vfs"
    src = t / "f.c"
    src.write_text('void lifted(int *c) __asm__("_Z13func_0037DE70P11ppu_context");\n'
                   "void lifted(int *c) { for (int i = 0; i < 9; i++) c[0] = c[0] * 3 + i; }\n"
                   "int main(void) { int c = 1; lifted(&c); return c & 1; }\n")
    r = subprocess.run([cc, "-O0", "-o", str(w / "build" / "KZ1HD"), str(src)], capture_output=True, text=True)
    check(r.returncode == 0, f"the ELF stand-in builds: {r.stderr}")
    table = symbols.load(str(w / "build" / "KZ1HD"))
    func = next((a for a, _, n in (table.syms if table else []) if n == "_Z13func_0037DE70P11ppu_context"), None)
    check(func is not None, "the ELF's .symtab has the lifted function")
    func = func or 0
    (w / "logs").mkdir()
    (w / "crash").mkdir()
    (w / "play.log").write_text(
        "[kz] Project Vekta 9.8.7 (patch set 55c305b52dd2f64e, ps3recomp ae92407)\n[kz-sys] os: Linux 6.1\n[kz-sys] gpu: AMD Radeon RX 6600\n"
        "[kz-sys] driver: Mesa 24.0\n[kz-sys] started-by: zip\n"
        f"[vfs] {home}/games/kz\n[kz-crash] begin\n[kz-crash] signature: KZ-1A2B-3C4D\n[kz-crash] end\n")
    for i in range(4):
        (w / "logs" / f"play-2026112{i}-000000.log").write_text(f"old {i} {home}\n")
    for i in range(12):
        p = w / "crash" / f"crash-202611{i + 10:02d}-000000.txt"
        p.write_text(f"begin\nsignal: 11 SIGSEGV\n#0 KZ1HD+0x{func + 0x10:x}\n#1 libc.so.6+0x10\nend\n")
        os.utime(p, (1700000000 + i, 1700000000 + i))
    (w / "crash" / "EBOOT.elf.txt").write_text("x")   # a .txt name that a game-file pattern refuses
    sessions.write_journal(str(w), [{"start": "2026-11-23T10:00:00", "end": "2026-11-23T10:01:12", "seconds": 72,
                                     "exit": -11, "exit_text": "-11", "meaning": "signal 11, SIGSEGV", "kind": "crash",
                                     "log": "play-20261123-100000.log", "signature": "KZ-1A2B-3C4D"}])
    (data / "kz1hd_pc.ini").write_text("fov=90\n")
    (root / "work" / "paths.env").write_text(f"KZ_GAME={home}/secret\n")

    def fake_system():
        return ({"system/linux-kernel.txt": f"2026-11-23T10:01:10 {os.uname().nodename} kernel: amdgpu: ring gfx "
                                            "timeout\n"}, ["Linux kernel log (last 24 h, GPU lines): 1 lines"])
    dest, skipped = support.build(str(root), str(data), str(t / "out"), firstrun.redactor(home=home, windows=False),
                                  "20261123-000000", note=f"I was in level 1, my files in {home}/x", system=fake_system)
    with zipfile.ZipFile(dest) as z:
        names = sorted(z.namelist())
        body = {n: z.read(n).decode() for n in names}
    crash_names = [n for n in names if re.fullmatch(r"crash/crash-\d{8}-\d{6}\.txt", n)]
    check(len(crash_names) == 10 and "crash/crash-20261110-000000.txt" not in names and
          "crash/crash-20261121-000000.txt" in names, f"the newest ten crash reports: {crash_names}")
    check([n for n in names if n.startswith("logs/play-")] ==
          [f"logs/play-2026112{i}-000000.log" for i in (1, 2, 3)], "the three newest earlier logs")
    check("logs/sessions.jsonl" in names and "play.log" in names and "kz1hd_pc.ini" in names and "report.txt" in names
          and "system/linux-kernel.txt" in names, f"sessions.jsonl, play.log, the settings, report.txt: {names}")
    sym = body.get("crash/crash-20261121-000000.symbolized.txt", "")
    check("func_0037DE70+0x10" in sym and "libc.so.6+0x10\n" in sym, f"the frames named from the ELF: {sym!r}")
    check(not any(home in v for v in body.values()) and not any("/home/" in v for v in body.values()),
          "no home folder path anywhere")
    check(os.uname().nodename not in "".join(body.values()) or len(os.uname().nodename) < 4,
          "the PC's name is taken out")
    check(not any(support.refused(n) for n in names) and "paths.env" not in " ".join(names) and
          not any("eboot" in n.lower() for n in names), f"no game-file name, no paths.env: {names}")
    f = body["facts.txt"]
    check("Port version: 9.8.7" in f and "GPU: AMD Radeon RX 6600" in f and "Driver: Mesa 24.0" in f and
          "Crash signature: KZ-1A2B-3C4D" in f and "Last session: exit -11" in f and "Sources included:" in f and
          "crash and hang reports (work/crash): 10" in f and "Linux kernel log" in f and "Frame names: 10 frames" in f
          and "earlier game logs: 3" in f and "Patch set: 55c305b52dd2f64e" in f, f"facts.txt: {f}")
    # The kernel log's host field goes, whatever name it holds (boot lines keep the distribution's default)
    kfiles, _ = support.linux_kernel(run=lambda *a, **k: "2026-11-23T08:00:01-03:00 cachyos kernel: amdgpu: ring gfx "
                                                         "timeout\n2026-11-23T08:00:02-03:00 cachyos kernel: usb 1-1\n")
    check(kfiles == {"system/linux-kernel.txt": "2026-11-23T08:00:01-03:00 kernel: amdgpu: ring gfx timeout\n"},
          f"the kernel lines without the host field: {kfiles}")
    # The bug form's link
    url = launch.issue_url(str(root), {"KZ_STARTED_BY": "zip", "HOME": home})
    from urllib.parse import parse_qs, urlparse
    q = {k: v[0] for k, v in parse_qs(urlparse(url).query).items()}
    form = (ROOT / ".github" / "ISSUE_TEMPLATE" / "bug_report.yml").read_text()
    ids = set(re.findall(r"^\s+id: (\w+)$", form, re.M))
    check(url.startswith("https://github.com/gustavomorinaga/project-vekta/issues/new?") and
          q["template"] == "bug_report.yml" and set(q) - {"template"} <= ids and q["version"] == "9.8.7" and
          q["platform"] == "Linux" and "KZ-1A2B-3C4D" in q["logs"] and q["install"] == "Linux folder",
          f"the bug form's link fills the template's own fields: {q}")
    for field in ("platform", "install"):
        opts = re.search(rf"id: {field}\n(?:.*\n)*?\s+options: (\[.*\])", form)
        check(opts and q[field] in json.loads(opts.group(1)), f"{field}={q[field]} is one of the form's options")
    check(home not in url, "the link holds no home path")

print("test_sessions:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
