#!/usr/bin/env python3
"""Host test for tools/release/firstrun.py on a copy of the tree whose build scripts are stand-ins that only record
that they ran: the first run runs every step, writes work/paths.env, the markers and the build stamp; a second run
skips all but the check and the compile; a changed lift input re-runs the lift and nothing before it; a failed step
leaves no marker and the next run resumes there; a failed check names the files and stops; tools/env.sh reads
work/paths.env, and a variable set in the environment wins; the status file, the progress lines, the job count, the
log's home folder, --dry-run and --interactive. No game, no network."""
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import firstrun  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


STUB = """#!/bin/sh
echo "{name}" >> "$KZ_WORK/calls"
[ "$FAIL_STEP" = "{name}" ] && {{ echo "error: {name} broke"; exit 3; }}
{extra}
exit 0
"""


def fake_tree(tmp):
    root = tmp / "tree"
    for rel in ("tools/env.sh", "VERSION"):
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / rel, root / rel)
    for rel in ("port/patches/series", "re/function_seeds.txt", "third_party/ps3recomp/requirements.txt",
                "tools/extract_spu_modules.py", "tools/psarc_ls.py", "port/kz1hd/native/a.c"):
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(rel + "\n")
    (root / "third_party/ps3recomp.pin").write_text("4e8f3ca\n")
    (root / "tools/kzbuild.py").write_text(
        "import os, sys\n"
        "cmd, w = sys.argv[1], os.environ['KZ_WORK']\n"
        "open(os.path.join(w, 'calls'), 'a').write(cmd + '\\n')\n"
        "if os.environ.get('FAIL_STEP') == cmd:\n"
        "    print('error: ' + cmd + ' broke')\n"
        "    sys.exit(3)\n"
        "if cmd == 'lift':\n"
        "    open(os.path.join(w, 'calls'), 'a').write(f\"jobs={os.environ['KZ_JOBS']} eboot={os.environ['KZ_EBOOT']}\\n\")\n"
        "if cmd == 'build':\n"
        "    print('[1/2] cc a')\n"
        "    print('[2/2] link KZ1HD')\n")
    (root / "tools/check-game-files.py").write_text(
        "import os, sys\n"
        "open(os.path.join(os.environ['KZ_WORK'], 'calls'), 'a').write('check\\n')\n"
        "if os.environ.get('FAIL_STEP') == 'check':\n"
        "    print('MISSING  EBOOT.elf (required): nothing at ' + os.environ['KZ_EBOOT'])\n"
        "    print('ok       PARAM.SFO')\n"
        "    sys.exit(1)\n"
        "print('ok       EBOOT.elf')\n")
    venv = root / "work/venv/bin"
    venv.mkdir(parents=True)
    (venv / "python").write_text("#!/bin/sh\n")
    (venv / "pip").write_text(STUB.format(name="pip", extra=""))
    for p in [venv / "python", venv / "pip"] + list((root / "tools").glob("*.sh")):
        p.chmod(0o755)
    game = tmp / "my files"
    game.mkdir()
    for f in ("EBOOT.elf", "data_ps3.psarc", "libsre.prx"):
        (game / f).write_text(f)
    return root, game


def ctx(root, game, **kw):
    paths = firstrun.resolve_paths(str(root), str(root / "work"), {"eboot": str(game / "EBOOT.elf"),
                                                                  "psarc": str(game / "data_ps3.psarc"),
                                                                  "libsre": str(game / "libsre.prx"),
                                                                  "game": str(game)})
    return firstrun.Ctx(root=str(root), paths=paths, jobs=3, work=str(root / "work"), **kw)


def run(root, game, fail=None, status=None):
    calls = root / "work/calls"
    if calls.exists():
        calls.unlink()
    os.environ.pop("FAIL_STEP", None)
    if fail:
        os.environ["FAIL_STEP"] = fail
    c = ctx(root, game)
    out = io.StringIO()
    log = firstrun.new_log(c, firstrun.redactor())
    r = firstrun.Runner(c, firstrun.STEPS, log, out=out, status_file=status)
    rc = r.run()
    log.close()
    os.environ.pop("FAIL_STEP", None)
    got = [ln for ln in calls.read_text().split("\n") if ln] if calls.exists() else []
    return rc, out.getvalue(), [x for x in got if "=" not in x], r, got


for var in ("KZ_GAME", "KZ_EBOOT", "KZ_PSARC", "KZ_LIBSRE", "PS3_DEV_FLASH", "KZ_WORK"):
    os.environ.pop(var, None)

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    root, game = fake_tree(tmp)
    status = str(tmp / "status.txt")

    rc, out, calls, r, raw = run(root, game, status=status)
    check(rc == 0 and "RESULT ok=1 step=-" in out, "the first run builds: " + out[-300:])
    check(calls == ["check", "pip", "patch", "lift", "vfs", "build"], f"every step runs: {calls}")
    check(f"jobs=3 eboot={game}/EBOOT.elf" in raw, f"the steps get the paths and the job count: {raw}")
    pcts = [float(ln.rsplit("pct=", 1)[1]) for ln in out.splitlines() if ln.startswith("PROGRESS ")]
    check(pcts == sorted(pcts) and pcts[-1] == 100.0, f"the progress only grows, to 100: {pcts[-5:]}")
    check("name=compile done=2 total=2" in out, "Ninja's [x/y] lines are the compile's progress")
    check("RESULT ok=1" in pathlib.Path(status).read_text(), "the status file holds the result")
    check((root / "work/build-stamp.json").is_file(), "the build stamp is written")
    check(len(list((root / "work/.firstrun").iterdir())) == len(firstrun.STEPS), "each step leaves a marker")
    check(firstrun.read_paths(str(root / "work"))["eboot"] == str(game / "EBOOT.elf"), "paths.env keeps the paths")

    rc, out, calls, _, _ = run(root, game)
    check(rc == 0 and calls == ["check", "build"], f"a second run skips the finished steps: {calls}")

    (root / "re/function_seeds.txt").write_text("changed\n")
    rc, out, calls, _, _ = run(root, game)
    check(calls == ["check", "lift", "build"], f"a changed lift input re-runs the lift only: {calls}")

    (root / "port/patches/series").write_text("changed\n")
    rc, out, calls, _, _ = run(root, game, fail="lift")
    check(rc == 1 and "RESULT ok=0 step=lift" in out and "REASON" in out and "lift broke" in out,
          "a failed step says which and why: " + out[-300:])
    rc, out, calls, _, _ = run(root, game)
    check(calls == ["check", "lift", "build"], f"the next run resumes at the failed step: {calls}")

    (game / "EBOOT.elf").write_text("another EBOOT")
    os.utime(game / "EBOOT.elf", (1, 1))
    rc, out, calls, _, _ = run(root, game)
    check(calls == ["check", "lift", "vfs", "build"], f"another EBOOT re-runs the lift and the vfs: {calls}")

    rc, out, calls, r, _ = run(root, game, fail="check", status=status)
    check(rc == 1 and calls == ["check"] and "REASON MISSING EBOOT.elf" in out, "a failed check stops the run")
    check(r.fail_inputs == {"eboot"}, f"a failed check names the inputs: {r.fail_inputs}")
    check("REASON MISSING" in pathlib.Path(status).read_text(), "the status file has the reasons")

    # an install over an earlier release: a build folder from another version or patch set is emptied before the
    # compile (its objects are newer than the release's fixed-date sources, so Ninja would keep them), one from this
    # tree is kept
    bdir = pathlib.Path(firstrun.build_dir(ctx(root, game)))
    bdir.mkdir(parents=True, exist_ok=True)
    (bdir / "CMakeCache.txt").write_text("PS3RECOMP_FFX_DIR:PATH=/old\n")
    rc, out, calls, _, _ = run(root, game)
    check(rc == 0 and (bdir / "CMakeCache.txt").is_file() and "fresh CMake" not in out,
          "a cache from this tree's own build is kept")
    (root / "work/ps3recomp-patched").mkdir(parents=True, exist_ok=True)
    (root / "work/ps3recomp-patched/.patchset").write_text("new-series\n")
    (bdir / "sys_ppu_thread.c.obj").write_text("an object of the earlier version")
    rc, out, calls, _, _ = run(root, game)
    check(rc == 0 and not (bdir / "CMakeCache.txt").exists() and not (bdir / "sys_ppu_thread.c.obj").exists()
          and "a clean build" in out, f"another patch set empties the build folder before the compile: {out[-300:]}")
    stamp = firstrun.read_stamp(str(root / "work"))
    check(stamp.get("patchset") == "new-series", f"the stamp then names the new patch set: {stamp}")
    bdir.mkdir(parents=True, exist_ok=True)
    (bdir / "CMakeCache.txt").write_text("x\n")
    stamp["version"] = "0.1.0"
    (root / "work/build-stamp.json").write_text(firstrun.json.dumps(stamp))
    rc, out, calls, _, _ = run(root, game)
    check(not bdir.exists(), "another version empties the build folder before the compile")
    bdir.mkdir(parents=True, exist_ok=True)
    (bdir / "CMakeCache.txt").write_text("x\n")
    (root / "work/build-stamp.json").unlink()
    check(firstrun.before_compile(ctx(root, game)) and not bdir.exists(),
          "a build folder without a stamp is built afresh")
    # a build folder outside work/ (KZ_BUILD set by hand) only loses its cache
    outside = tmp / "own-build"
    outside.mkdir()
    (outside / "CMakeCache.txt").write_text("x\n")
    (outside / "keep.obj").write_text("x")
    os.environ["KZ_BUILD"] = str(outside)
    try:
        msg = firstrun.before_compile(ctx(root, game))
    finally:
        del os.environ["KZ_BUILD"]
    check(msg and not (outside / "CMakeCache.txt").exists() and (outside / "keep.obj").exists(),
          f"a build folder outside work/ keeps its objects: {msg}")
    run(root, game)

    # tools/env.sh reads paths.env; a variable already set wins; odd characters survive
    odd = str(tmp / 'a "b" $c `d` \\e')
    firstrun.write_paths(str(root / "work"), {"eboot": odd, "game": str(game)})
    check(firstrun.read_paths(str(root / "work"))["eboot"] == odd, "paths.env reads back what it wrote")
    sh = lambda env: subprocess.run(["sh", "-c", '. "$KZ_ROOT/tools/env.sh"; printf %s "$KZ_EBOOT"'],  # noqa: E731
                                    env=dict(os.environ, KZ_ROOT=str(root), **env), capture_output=True,
                                    text=True).stdout
    check(sh({}) == odd, f"tools/env.sh takes the saved path: {sh({})!r}")
    check(sh({"KZ_EBOOT": "/mine"}) == "/mine", "a variable set in the environment wins")
    check(firstrun.resolve_paths(str(root), str(root / "work"), {})["eboot"] == odd, "a run takes the saved paths")
    check(firstrun.resolve_paths(str(root), str(root / "work"), {"eboot": "/x"})["eboot"] == "/x",
          "an option beats the saved path")

    # --dry-run writes nothing
    before = sorted(p.name for p in (root / "work").iterdir())
    out = io.StringIO()
    firstrun.dry_run(ctx(root, game), firstrun.STEPS, out=out)
    check(sorted(p.name for p in (root / "work").iterdir()) == before and "kzbuild.py" in out.getvalue(),
          "--dry-run prints the plan and writes nothing")

    # --interactive: a failed check asks for the failing file only, then asks once about DLSS (Linux), then builds
    answers = iter(["", str(tmp / "nowhere.elf"), "", "", "", "", str(game / "EBOOT.elf")] +
                   (["n"] if os.name != "nt" else []))
    real_license = firstrun.fetch_license
    firstrun.fetch_license = lambda c: str(tmp / "LICENSE.txt")   # no network in a test
    asked = []

    def ask(q):
        asked.append(q)
        return next(answers)

    class A:
        status_file = stop_file = None
        desktop = False
    c = ctx(root, game)
    os.environ["FAIL_STEP"] = ""
    real = firstrun.Runner.run

    def run_once(self):
        # the first check fails because the EBOOT path does not exist
        if self.steps[0].name == "check" and not os.path.exists(self.ctx.paths["eboot"]):
            os.environ["FAIL_STEP"] = "check"
        rc = real(self)
        os.environ["FAIL_STEP"] = ""
        return rc
    firstrun.Runner.run = run_once
    said = []
    console = io.StringIO()
    rc = firstrun.interactive(c, A(), ask=ask, say=said.append, out=console)
    firstrun.Runner.run = real
    firstrun.fetch_license = real_license
    os.environ.pop("FAIL_STEP", None)
    check(rc == 0, f"--interactive builds once the check passes: {said[-2:]}")
    if os.name != "nt":
        check(any(firstrun.DLSS_LICENSE_URL in s and str(tmp / "LICENSE.txt") in s for s in said) and
              c.dlss == "decline" and firstrun.read_dlss(c.work) == "decline" and firstrun.cmd_dlss(c) == [],
              "--interactive asks about DLSS once, with the licence's link and text; no is kept and fetches nothing")
    check("PROGRESS " not in console.getvalue() and "RESULT " not in console.getvalue() and "100.0%" in console.getvalue(),
          "the console shows the bar, not the protocol lines")
    check(sum("EBOOT" in q for q in asked) == 2 and len([q for q in asked if "DLSS" not in q]) == 7,
          f"after a failed check only the failing file is asked again: {len(asked)} questions")

# the applications-menu entry: written under $XDG_DATA_HOME, quoted for the Exec rules, removed again
with tempfile.TemporaryDirectory() as tmp:
    os.environ["XDG_DATA_HOME"] = tmp
    odd = '/opt/my "odd" $dir'
    p = firstrun.install_desktop(odd)
    text = pathlib.Path(p).read_text()
    check(p == os.path.join(tmp, "applications", "project-vekta.desktop"), f"the entry goes to the menu folder: {p}")
    check('Exec="/opt/my \\\\"odd\\\\" \\\\$dir/tools/play.sh"' in text, f"Exec is quoted and escaped: {text}")
    check(f"Icon={odd}/assets/icon.svg" in text and "Categories=Game;" in text, "the entry has the icon and category")
    if shutil.which("desktop-file-validate"):
        r = subprocess.run(["desktop-file-validate", p], capture_output=True, text=True)
        check(r.returncode == 0, "desktop-file-validate accepts it: " + r.stdout + r.stderr)
    check(firstrun.remove_desktop() == p and not os.path.exists(p) and firstrun.remove_desktop() is None,
          "--remove-desktop removes it, once")
    del os.environ["XDG_DATA_HOME"]
check((ROOT / "assets/icon.svg").is_file(), "the icon the entry names is in the tree")

# --save-paths (the Windows setup program): writes only the paths given, keeps the others, runs no step
with tempfile.TemporaryDirectory() as tmp:
    work = os.path.join(tmp, "work")
    os.environ["KZ_WORK"] = work
    try:
        check(firstrun.main(["--root", tmp, "--save-paths"]) == 2, "--save-paths without a path is refused")
        win = "C:\\Games\\Kill zone\\EBOOT.elf"
        check(firstrun.main(["--root", tmp, "--save-paths", "--game", "/g/NPUA80856"]) == 0, "--save-paths writes")
        check(firstrun.main(["--root", tmp, "--save-paths", "--eboot", "/g/EBOOT.elf"]) == 0, "a second one adds")
        saved = firstrun.read_paths(work)
        check(saved == {"game": "/g/NPUA80856", "eboot": "/g/EBOOT.elf"}, f"both kept, nothing else: {saved}")
        firstrun.write_paths(work, {"eboot": win})
        check(firstrun.read_paths(work) == {"eboot": win}, "a Windows path with spaces reads back the same")
        check(sorted(os.listdir(work)) == ["paths.env"], "no step ran: only paths.env in work/")
    finally:
        del os.environ["KZ_WORK"]

check(firstrun.jobs_for(None, cores=16) == 4, "unknown memory: four jobs")
check(firstrun.jobs_for(32 * firstrun.GIB, cores=16) == 16, "32 GiB free and 16 cores: 16 jobs")
check(firstrun.jobs_for(6 * firstrun.GIB, cores=16) == 3, "6 GiB free: 3 jobs")
check(firstrun.jobs_for(2 * firstrun.GIB, cores=16) == 1, "little memory: one job")
w = firstrun.redactor("D:\\Profiles\\jane", windows=True)
for leak in ("D:\\Profiles\\jane\\x", "D:/Profiles/jane/x", "d:/profiles/JANE/x", "D_/Profiles/jane/x"):
    check(w(leak) == "~" + leak[len("D:/Profiles/jane"):], f"Windows home in any spelling: {leak} -> {w(leak)}")
check(w("D:/Profiles/janet/x") == "D:/Profiles/janet/x", "another user's folder is left alone")
red = firstrun.redactor("/srv/players/jane")
check(red("/srv/players/jane/Games and /srv/players/janet/x") == "~/Games and /srv/players/janet/x", "the log writes the home as ~")

print("test_firstrun:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
