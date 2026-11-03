#!/usr/bin/env python3
"""Host test for the Windows setup program's pieces: installer/vekta.iss names files the payload has (its LICENSE,
VERSION and the launcher) and its art; the launcher (installer/launcher/launcher.c) starts the Python and the script
at the paths assemble_payload.py gives them, and passes --build-if-needed, which launch.py takes; the launcher's
version block comes from VERSION; launch.py --build-if-needed without run.cmd says what is missing; the Inno Setup
pin is there; .cmd and .iss files keep CRLF; the pages check check-game-files.py's sizes, and the build in the
wizard passes firstrun.py's own options and labels its steps. No compiler and no Wine: tools/release/build_launcher.py and
build_setup.py run on the maintainer's machine (docs/BUILDING.md)."""
import io
import os
import json
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
sys.path.insert(0, str(ROOT / "tools"))
import assemble_payload as ap  # noqa: E402
import build_launcher  # noqa: E402
import launch  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


iss = (ROOT / "installer" / "vekta.iss").read_bytes()
check(b"\r\n" in iss and b"\n" not in iss.replace(b"\r\n", b""), "vekta.iss has CRLF line endings")
text = iss.decode()
for ref in re.findall(r'\{#Payload\}\\\\?([^"\s;]+)', text):
    check(ref == "*" or ref == "VERSION" or ref == "LICENSE" or (ROOT / ref.replace("\\", "/")).exists(),
          f"vekta.iss names a payload file the payload has: {ref}")
check('Source: "{#Payload}\\work\\ps3recomp-patched\\.patchset"' in text,
      "vekta.iss names .patchset itself (a wildcard skips hidden files)")
check('#define LauncherExe "Project Vekta.exe"' in text, "vekta.iss starts the launcher assemble_payload ships")
check((ROOT / "installer" / "art" / "vekta.ico").is_file() and "SetupIconFile=art\\vekta-setup.ico" in text,
      "the setup icon is there")
# The look: every image and icon the script names is in installer/art and matches art.json (installer_art.py --check)
for f in re.findall(r"art\\([\w-]+\.(?:bmp|ico))", text):
    check((ROOT / "installer" / "art" / f).is_file(), f"installer/art/{f} is there")
check("WizardStyle=modern dark" in text and "DisableWelcomePage=no" in text, "dark, with the Welcome page's image")
r = subprocess.run([sys.executable, str(ROOT / "tools/release/installer_art.py"), "--check", "--fonts",
                    str(ROOT / "work" / "no-fonts")], capture_output=True, text=True)
check(r.returncode == 0, f"the art is what installer_art.py made: {r.stdout}")
check("PrivilegesRequired=lowest" in text and "{localappdata}\\Programs\\ProjectVekta" in text,
      "a per-user install, by default into a folder without spaces")
# The pages check the files tools/check-game-files.py checks, with its sizes
import importlib.util  # noqa: E402
spec = importlib.util.spec_from_file_location("cgf", ROOT / "tools" / "check-game-files.py")
cgf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cgf)
sizes = {label: size for label, _p, size, *_ in cgf.FILES}
for define, label in (("EbootSize", "EBOOT.elf"), ("PsarcSize", "data_ps3.psarc"), ("LibsreSize", "libsre.prx")):
    m = re.search(r'#define %s "(\d+)"' % define, text)
    check(m and int(m.group(1)) == sizes[label], f"vekta.iss {define} is check-game-files.py's size of {label}")
# The build in the wizard runs firstrun.py with options it has, and names its steps
import firstrun  # noqa: E402
opts = set(re.findall(r"' --([a-z-]+)", text))
known = {n.replace("_", "-") for n, *_ in firstrun.INPUTS} | {"root", "status-file", "stop-file", "save-paths"}
check(opts and opts <= known, f"vekta.iss passes only firstrun.py's options: {sorted(opts - known)}")
src = (ROOT / "tools/release/firstrun.py").read_text()
for opt in ("root", "status-file", "stop-file", "save-paths"):
    check(f'"--{opt}"' in src, f"firstrun.py has --{opt}")
labels = set(re.findall(r"Name = '([a-z-]+)' then Result", text))
steps = {st.name for st in firstrun.STEPS}
check(labels and labels <= steps and steps - labels <= {"stamp"}, f"StepLabel names firstrun.py's steps: {labels} {steps}")
check("tools\\release\\firstrun.py" in text and "work\\toolchain-cache\\python\\python.exe" in text,
      "the wizard runs the payload's firstrun.py with its Python")
for param in ("GAME", "EBOOT", "PSARC", "LIBSRE", "DEVFLASH"):
    check(f"Pick('{param}'" in text, f"/{param}= is taken on the command line")

for rel in ("installer/run.cmd", "installer/play.cmd"):
    data = (ROOT / rel).read_bytes()
    check(b"\n" not in data.replace(b"\r\n", b""), f"{rel} has CRLF line endings")

c = (ROOT / "installer" / "launcher" / "launcher.c").read_text()
check("\\\\work\\\\toolchain-cache\\\\python\\\\pythonw.exe" in c and ap.CACHE == "work/toolchain-cache",
      "the launcher's Python is where the payload puts it")
check("\\\\tools\\\\release\\\\launch.py" in c and "--build-if-needed" in c, "the launcher runs launch.py with "
      "--build-if-needed")
check(build_launcher.version_header("0.2.1") == '#define VEKTA_VER_COMMA 0,2,1,0\n#define VEKTA_VER_STR "0.2.1"\n',
      "the version block from VERSION")
rc = (ROOT / "installer" / "launcher" / "launcher.rc").read_text()
check('1 ICON "vekta.ico"' in rc and "VEKTA_VER_STR" in rc, "launcher.rc: icon and version")

m = json.loads((ROOT / "tools" / "release" / "toolchain.json").read_text())
inno = [t for t in m.get("build_tools", []) if t["name"] == "innosetup"]
check(len(inno) == 1 and re.fullmatch(r"[0-9a-f]{64}", inno[0]["sha256"]) and inno[0]["url"].startswith("https://"),
      "Inno Setup is pinned by SHA-256")

with tempfile.TemporaryDirectory() as tmp:
    err = io.StringIO()
    se = sys.stderr
    sys.stderr = err
    try:
        r = launch.main(["--root", tmp, "--build-if-needed"])
    finally:
        sys.stderr = se
    check(r == 1 and "no game folder" in err.getvalue(), f"no run.cmd: launch says what is missing: {err.getvalue()}")
    try:
        ap.plan(str(ROOT), tmp, str(pathlib.Path(tmp) / "missing.exe"))
        check(False, "a missing launcher refuses the payload")
    except ap.Refused as e:
        check("no launcher" in str(e), str(e))

# Where the saves go: the run folder without vekta-install.ini or when portable, else %APPDATA%\Project Vekta; the
# setup writes the key launch.py reads; an earlier version's saves move once and never over the player's files
with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    run_folder, appdata = str(t / "work" / "vfs"), str(t / "appdata")
    env = {"APPDATA": appdata}
    check(launch.data_dir(tmp, run_folder, env) == run_folder, "no vekta-install.ini (Git clone, zip): the run folder")
    (t / launch.INSTALL_INI).write_text("[install]\r\nportable=1\r\n")
    check(launch.data_dir(tmp, run_folder, env) == run_folder, "portable=1: the run folder")
    (t / launch.INSTALL_INI).write_text("[install]\r\nportable=0\r\n")
    data = launch.data_dir(tmp, run_folder, env)
    check(data == str(t / "appdata" / "Project Vekta"), f"portable=0: %APPDATA%: {data}")
    check(launch.data_dir(tmp, run_folder, {}) == run_folder, "no %APPDATA%: the run folder")
    (t / launch.INSTALL_INI).write_text("not an ini")
    check(launch.data_dir(tmp, run_folder, env) == run_folder, "an unreadable ini: the saves stay where they are")
    os.makedirs(os.path.join(run_folder, "gamedata", "dev_hdd0"))
    pathlib.Path(run_folder, "kz1hd_pc.ini").write_text("old")
    pathlib.Path(run_folder, "kz1hd_keys.ini").write_text("old keys")
    os.makedirs(data)
    pathlib.Path(data, "kz1hd_keys.ini").write_text("new keys")
    moved = launch.migrate(run_folder, data)
    check(moved == ["kz1hd_pc.ini", "gamedata"] and os.path.isdir(os.path.join(data, "gamedata", "dev_hdd0")),
          f"the old saves and settings moved: {moved}")
    check(pathlib.Path(data, "kz1hd_keys.ini").read_text() == "new keys" and
          pathlib.Path(run_folder, "kz1hd_keys.ini").exists(), "a file already in the data folder is kept")
    check(launch.migrate(run_folder, run_folder) == [], "portable: nothing moves")
iss_text = (ROOT / "installer" / "vekta.iss").read_text()
for v in ("1", "0"):
    check(re.search(rf'Filename: "{{app}}\\{launch.INSTALL_INI}"; Section: "install"; Key: "portable"; '
                    rf'String: "{v}"', iss_text), f"the setup writes portable={v} in {launch.INSTALL_INI}")

# The Start menu's entries run the launcher with options launch.py takes
import support  # noqa: E402
for opt in re.findall(r'Filename: "\{app\}\\\{#LauncherExe\}"; Parameters: "([^"]+)"', iss_text):
    check(f'"{opt}"' in (ROOT / "tools/release/launch.py").read_text(), f"launch.py takes {opt}")
check(len(re.findall(r'Parameters: "--', iss_text)) == 3, "three entries: repair, saves folder, support package")

# --repair: the install's files against manifest.json, then firstrun.py; its message says how it went
import hashlib  # noqa: E402
with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    (t / "a.txt").write_text("a")
    (t / "zlib").mkdir()
    (t / "zlib" / "zconf.h.included").write_text("z")
    files = [{"path": "a.txt", "size": 1, "sha256": hashlib.sha256(b"a").hexdigest()},
             {"path": "zlib/zconf.h", "size": 1, "sha256": hashlib.sha256(b"z").hexdigest()}]
    (t / "manifest.json").write_text(json.dumps({"files": files}))
    check(launch.verify_install(tmp) == (2, []), "intact, and a file CMake renamed counts as there")
    (t / "a.txt").write_text("b")
    n, bad = launch.verify_install(tmp)
    check(bad == ["a.txt (changed)"], f"a changed file is named: {bad}")
    said, asked, ran = [], [], []
    old = launch.notify, launch.ask
    launch.notify = lambda text, error=True: said.append((text, error))
    launch.ask = lambda text: asked.append(text) or True
    try:
        check(launch.repair(tmp, run=lambda *a, **k: ran.append(a)) == 1 and not ran and "setup program again" in
              said[-1][0], "a damaged install is not rebuilt")
        (t / "a.txt").write_text("a")

        class Done:
            def __init__(self, rc, lines):
                self.rc, self.lines = rc, lines

            def __call__(self, argv, **k):
                ran.append(argv)
                st = argv[argv.index("--status-file") + 1]
                pathlib.Path(st).write_text("LOG x\n" + "\n".join(self.lines) + "\n")
                return subprocess.CompletedProcess(argv, self.rc)
        check(launch.repair(tmp, run=Done(0, ["RESULT ok=1 step=- elapsed=3"])) == 0 and "Verified" in said[-1][0] and
              "--bar" in ran[-1] and ran[-1][1].endswith("firstrun.py"), f"a rebuild that passes: {said[-1]}")
        check(launch.repair(tmp, run=Done(1, ["RESULT ok=0 step=check", "REASON EBOOT.elf: DIFFERENT"])) == 1 and
              "EBOOT.elf: DIFFERENT" in said[-1][0], f"the reason a rebuild stopped: {said[-1]}")
    finally:
        launch.notify, launch.ask = old

# The support package: logs and settings, the home folder taken out, never saves or game files
with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    root, data, out = t / "app", t / "data", t / "out"
    (root / "work" / "logs").mkdir(parents=True)
    (data / "gamedata").mkdir(parents=True)
    (root / "work" / "play.log").write_text("using device 0: NVIDIA GeForce RTX 3070 Ti\nD:\\Profiles\\player\\x\n")
    for i in range(3):
        (root / "work" / "logs" / f"firstrun-2026110{i}-000000.log").write_text(f"run {i}\n")
    (root / "manifest.json").write_text(json.dumps({"version": "9.9.9", "commit": "abc", "patchset": "p"}))
    (data / "kz1hd_pc.ini").write_text("fov=90\n")
    (data / "gamedata" / "SAVE.DAT").write_text("save")
    (root / "work" / "paths.env").write_text("KZ_GAME=secret\n")

    def red(s):
        return s.replace("D:\\Profiles\\player", "~")
    dest, skipped = support.build(str(root), str(data), str(out), red, "20261102-000000")
    import zipfile  # noqa: E402
    with zipfile.ZipFile(dest) as z:
        names = sorted(z.namelist())
        body = {n: z.read(n).decode() for n in names}
    check(names == ["facts.txt", "kz1hd_pc.ini", "logs/firstrun-20261101-000000.log",
                    "logs/firstrun-20261102-000000.log", "play.log"], f"the files: {names}")
    check("~\\x" in body["play.log"] and "player" not in "".join(body.values()), "the user folder is taken out")
    check("Port version: 9.9.9" in body["facts.txt"] and "GPU: NVIDIA GeForce RTX 3070 Ti" in body["facts.txt"],
          f"facts: {body['facts.txt']}")
    check(dest.endswith("project-vekta-support-20261102-000000.zip"), dest)
    check(support.refused("EBOOT.elf") and support.refused("x.psarc") and support.refused("SAVE.DAT") and
          not support.refused("play.log"), "only text names, never game files")
    texts = {"big.log": "line\n" * 5000, "small.ini": "a=1\n"}
    trimmed = support.trim(texts, 2000)
    check(trimmed == ["big.log"] and sum(len(v) for v in texts.values()) <= 2000 and
          texts["big.log"].startswith(support.MARKER), "the oldest lines of the largest log go first")
    check(support.build(str(t / "none"), str(t / "none"), str(out), red, "x") == ("", []), "nothing to collect")

# Stage 4: the uninstaller's saves folder is launch.py's; an install over an earlier one replaces only the payload's
# own folders (never the build, the game folder, the logs or the player's choices)
check("{userappdata}\\" + launch.SAVE_FOLDER + "'" in iss_text, "the uninstaller asks about launch.py's saves folder")
gone = re.findall(r'Type: filesandordirs; Name: "\{app\}\\([^"]+)"', iss_text.split("[InstallDelete]")[1].split("[")[0])
roots = {d.split("/")[0] if not d.startswith("work/") else "/".join(d.split("/")[:2]) for _, d, _, _ in ap.RULES}
roots |= {"work/toolchain-cache"}
check(gone and all(g.replace("\\", "/") in roots for g in gone), f"[InstallDelete] only the payload's folders: {gone}")
for kept in ("work\\vfs", "work\\lift", "work\\build-win", "work\\logs", "vekta-install.ini"):
    check(kept not in gone, f"an install over it keeps {kept}")
check("function PrepareToInstall" in iss_text and "InstallDirProblem(ExpandConstant('{app}'))" in iss_text,
      "a silent install with a bad /DIR= is refused too")

# The stand-ins release.yml installs with: the right kinds and sizes, refused only by their SHA-256
with tempfile.TemporaryDirectory() as tmp:
    r = subprocess.run([sys.executable, str(ROOT / "tools/release/fake_game_files.py"), tmp], capture_output=True,
                       text=True)
    sw = dict(line[1:].split("=", 1) for line in r.stdout.split())
    check(r.returncode == 0 and set(sw) == {"GAME", "EBOOT", "PSARC", "LIBSRE", "DEVFLASH"}, f"five switches: {sw}")
    env = dict(os.environ, KZ_GAME=sw["GAME"], KZ_EBOOT=sw["EBOOT"], KZ_PSARC=sw["PSARC"], KZ_LIBSRE=sw["LIBSRE"],
               PS3_DEV_FLASH=sw["DEVFLASH"])
    q = subprocess.run([sys.executable, str(ROOT / "tools/check-game-files.py"), "--quick"], capture_output=True,
                       text=True, env=env)
    for label in ("PARAM.SFO", "EBOOT.elf", "libsre.prx"):
        check(f"DIFFERENT {label}" in q.stdout, f"{label}: the right kind and size, another hash: {q.stdout}")
    check("ok       data_ps3.psarc" in q.stdout and q.returncode == 1, "the archive passes --quick; the check fails")

print("test_setup:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
