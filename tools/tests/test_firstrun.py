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
    stubs = {"ps3recomp-patched.sh": "", "lift.sh": 'echo "jobs=$KZ_JOBS eboot=$KZ_EBOOT" >> "$KZ_WORK/calls"',
             "setup-vfs.sh": "", "build.sh": 'echo "[1/2] cc a"; echo "[2/2] link KZ1HD"'}
    for name, extra in stubs.items():
        (root / "tools" / name).write_text(STUB.format(name=name.split(".")[0], extra=extra))
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
    check(calls == ["check", "pip", "ps3recomp-patched", "lift", "setup-vfs", "build"], f"every step runs: {calls}")
    check(f"jobs=3 eboot={game}/EBOOT.elf" in raw, f"the steps get the paths and the job count: {raw}")
    pcts = [float(ln.rsplit("pct=", 1)[1]) for ln in out.splitlines() if ln.startswith("PROGRESS ")]
    check(pcts == sorted(pcts) and pcts[-1] == 100.0, f"the progress only grows, to 100: {pcts[-5:]}")
    check("name=compile done=2 total=2" in out, "Ninja's [x/y] lines are the compile's progress")
    check("RESULT ok=1" in pathlib.Path(status).read_text(), "the status file holds the result")
    check((root / "work/build-stamp.json").is_file(), "the build stamp is written")
    check(len(list((root / "work/.firstrun").iterdir())) == 7, "each step leaves a marker")
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
    check(calls == ["check", "lift", "setup-vfs", "build"], f"another EBOOT re-runs the lift and the vfs: {calls}")

    rc, out, calls, r, _ = run(root, game, fail="check", status=status)
    check(rc == 1 and calls == ["check"] and "REASON MISSING EBOOT.elf" in out, "a failed check stops the run")
    check(r.fail_inputs == {"eboot"}, f"a failed check names the inputs: {r.fail_inputs}")
    check("REASON MISSING" in pathlib.Path(status).read_text(), "the status file has the reasons")

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
    check(sorted(p.name for p in (root / "work").iterdir()) == before and "lift.sh" in out.getvalue(),
          "--dry-run prints the plan and writes nothing")

    # --interactive: a failed check asks for the failing file only, then builds
    answers = iter(["", str(tmp / "nowhere.elf"), "", "", "", "", str(game / "EBOOT.elf")])
    asked = []

    def ask(q):
        asked.append(q)
        return next(answers)

    class A:
        status_file = stop_file = None
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
    os.environ.pop("FAIL_STEP", None)
    check(rc == 0, f"--interactive builds once the check passes: {said[-2:]}")
    check("PROGRESS " not in console.getvalue() and "RESULT " not in console.getvalue() and "100.0%" in console.getvalue(),
          "the console shows the bar, not the protocol lines")
    check(sum("EBOOT" in q for q in asked) == 2 and len(asked) == 7,
          f"after a failed check only the failing file is asked again: {len(asked)} questions")

check(firstrun.jobs_for(None, cores=16) == 4, "unknown memory: four jobs")
check(firstrun.jobs_for(32 * firstrun.GIB, cores=16) == 16, "32 GiB free and 16 cores: 16 jobs")
check(firstrun.jobs_for(6 * firstrun.GIB, cores=16) == 3, "6 GiB free: 3 jobs")
check(firstrun.jobs_for(2 * firstrun.GIB, cores=16) == 1, "little memory: one job")
red = firstrun.redactor("/home/jane")
check(red("/home/jane/Games and /home/janet/x") == "~/Games and /home/janet/x", "the log writes the home as ~")

print("test_firstrun:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
