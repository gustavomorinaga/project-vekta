#!/usr/bin/env python3
"""Host test for the Windows setup program's pieces: installer/vekta.iss names files the payload has (its LICENSE,
VERSION and the launcher) and its art; the launcher (installer/launcher/launcher.c) starts the Python and the script
at the paths assemble_payload.py gives them, and passes --build-if-needed, which launch.py takes; the launcher's
version block comes from VERSION; launch.py --build-if-needed in an empty folder says what is missing; the Inno Setup
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
    # work\toolchain-cache\python: the bundled Python assemble_payload.py fetches into the Windows payload (run.cmd's)
    # {#LauncherExe}: Project Vekta.exe, which assemble_payload.py --launcher puts at the payload's root
    check(ref in ("*", "VERSION", "LICENSE", "{#LauncherExe}") or ref.startswith("work\\toolchain-cache\\python\\")
          or (ROOT / ref.replace("\\", "/")).exists(),
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
sizes = {label: size for label, _p, size, *_ in cgf.FILES + [cgf.PSARC_FILE]}
for define, label in (("EbootSize", "EBOOT.elf"), ("PsarcSize", "data_ps3.psarc")):
    m = re.search(r'#define %s "(\d+)"' % define, text)
    check(m and int(m.group(1)) == sizes[label], f"vekta.iss {define} is check-game-files.py's size of {label}")
# dev_flash may appear only in [UninstallDelete]: the stand-in the game writes there and its empty folders (#132)
asked = re.sub(r"(?ms)^\[UninstallDelete\].*?(?=^\[)", "", text)
check("FlashPage" not in text and "#define Firmware" not in text and "dev_flash" not in asked,
      "the setup asks for no firmware (no dev_flash page)")
undel = re.search(r"(?ms)^\[UninstallDelete\](.*?)(?=^\[)", text).group(1)
check(r'Type: files; Name: "{app}\work\vfs\dev_flash\sys\external\flashMP3.pic"' in undel and
      r'Type: dirifempty; Name: "{app}\work\vfs"' in undel,
      "uninstall removes the game's flashMP3.pic stand-in and the build's empty work\\vfs folders (#132)")
check("'USRDIR\\data_ps3.psarc.edat'" in text, "the wizard takes the game's .edat when the archive is left empty")
# The build in the wizard runs firstrun.py with options it has, and names its steps
import firstrun  # noqa: E402
opts = set(re.findall(r"' --([a-z0-9-]+)", text))
known = {n.replace("_", "-") for n, *_ in firstrun.INPUTS} | {"root", "status-file", "stop-file", "save-paths", "dlss",
                                                              "rpcs3", "disc"}
known |= {"resolve", "out"}   # the "Your game" page's call of rpcs3_import.py
check(opts and opts <= known, f"vekta.iss passes only firstrun.py's and rpcs3_import.py's options: {sorted(opts - known)}")
# "Your game": the RPCS3 route asks rpcs3_import.py --resolve with the bundled Python before installing, skips the
# file pages when it is chosen, and hands the build --rpcs3 instead of the file paths
import rpcs3_import  # noqa: E402
check("rpcs3_import.py')) + ' --resolve' + Args + ' --out '" in text and "{tmp}\\rp\\python\\python.exe" in text
      and 'Source: "{#Payload}\\tools\\release\\rpcs3_locate.py"; DestDir: "{tmp}\\rp"; Flags: dontcopy' in text,
      "the RPCS3 check runs before the install, from a temporary copy of the Python and the two modules")
files_sec = text[text.index("[Files]"):text.index("[Icons]")]
check(files_sec.index("rpcs3_import.py\"; DestDir: \"{tmp}") < files_sec.index('Source: "{#Payload}\\*"')
      and 'Source: "{#Payload}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs solidbreak'
      in files_sec, "the RPCS3 helper is unpacked from its own small solid block, ahead of the payload (no freeze)")
check("ewWaitUntilTerminated" not in text[text.index("procedure CheckRpcs3(Hint: String);\r\nvar"):text.index("function UseRpcs3")]
      and "npbstMarquee" in text and "RpcsNote.Caption := 'Your RPCS3 cannot be used:'" in text,
      "the RPCS3 check shows a progress page, never blocks the window, and prints its reasons on the page")
check("RpcsBrowse.OnClick := @RpcsBrowseClick;" in text and "CheckRpcs3(D);" in text
      and "if Hint <> '' then Args := ' --rpcs3 ' + Quote(Hint);" in text and "DeleteFile(Res);" in text
      and "SourcePage.CheckListBox.ItemEnabled[0] := True;" in text
      and "CheckRpcs3(Pick('RPCS3', 'Rpcs3Dir'))" in text and "'Rpcs3Dir', RpcsHint" in text,
      "the page searches by itself and has Find RPCS3... for a folder the player picks (kept for next time)")
check("if (UseRpcs3 or UseDisc) and ((PageID = GamePage.ID) or (PageID = FilesPage.ID))" in text
      and "Result := ' --rpcs3 ' + Quote(RpcsRoot);" in text, "the RPCS3 route skips the file pages and builds with --rpcs3")
check(all(f"'{k}'" in text for k in ("status", "root", "game")) and "Pos('problem=', Lines[I]) = 1" in text
      and set(rpcs3_import.lines({"status": "ok", "root": "r", "game": "g", "exe": ["x"],
              "problems": []})[0].split("=")[:1]) == {"status"},
      "the page reads the keys rpcs3_import.py writes")
src = (ROOT / "tools/release/firstrun.py").read_text()
for opt in ("root", "status-file", "stop-file", "save-paths"):
    check(f'"--{opt}"' in src, f"firstrun.py has --{opt}")
labels = set(re.findall(r"Name = '([a-z-]+)' then Result", text))
steps = {st.name for st in firstrun.STEPS}
# "disc" (firstrun.py --disc, before the steps) is a PROGRESS name of its own, as rpcs3 is
check(labels and labels <= steps | {"disc"} and steps - labels <= {"stamp"},
      f"StepLabel names firstrun.py's steps: {labels} {steps}")
check("if (UseRpcs3 or UseDisc) and ((PageID = GamePage.ID) or (PageID = FilesPage.ID))" in text
      and "Result := ' --disc ' + Quote(RemoveBackslashUnlessRoot(DiscPage.Values[0]));" in text
      and "if (PageID = DiscPage.ID) and not UseDisc then Result := True;" in text
      and "SourcePage.Add('Show me how');" in text and "SourcePage.SelectedValueIndex = 3) then" in text,
      "the disc route has its page, skips the file pages and builds with --disc; Show me how is the last choice")
check("tools\\release\\firstrun.py" in text and "work\\toolchain-cache\\python\\python.exe" in text,
      "the wizard runs the payload's firstrun.py with its Python")
for param in ("GAME", "EBOOT", "PSARC", "DISC", "DLSS"):
    check(f"Pick('{param}'" in text, f"/{param}= is taken on the command line")
check("LIBSRE" not in text and "libsre" not in text.lower(), "no libsre.prx is asked for (the runtime's own LFQueue)")
# NVIDIA DLSS: an optional page whose box, unticked unless accepted before, accepts the licence firstrun.py shows;
# its answer goes to firstrun.py and work\dlss-choice, and setup ships no NVIDIA file
m_url = re.search(r'#define DlssLicenseURL "([^"]+)"', text)
check(m_url and m_url.group(1) == firstrun.DLSS_LICENSE_URL, "the setup links the licence firstrun.py names")
check("I accept the NVIDIA DLSS SDK license" in text and "DlssCheck.Checked := CompareText(Pick('DLSS', 'Dlss'), "
      "'accept') = 0" in text and "Checked := True" not in text, "the DLSS box is unticked unless accepted before")
check("' --dlss ' + DlssChoice" in text and "work\\dlss-choice'), DlssChoice" in text and
      "SetPreviousData(PreviousDataKey, 'Dlss'" in text, "the answer goes to firstrun.py, work\\dlss-choice and "
      "the next setup")
check(not re.search(r"(?im)^Source:.*(nvngx|nvsdk|dlss)", text), "setup names no NVIDIA file to install")
check('Name: "{app}\\work\\toolchain-cache\\dlss-sdk"' in text, "the uninstaller removes the DLSS download")

for rel in ("installer/run.cmd", "installer/play.cmd", "installer/prepare.cmd"):
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
    # a game folder named (KZ_GAME) but not there: the check fails the same way on every machine, and no RPCS3 of
    # this machine is looked for (with nothing given, the first start would search RPCS3's usual places)
    old_game = os.environ.get("KZ_GAME")
    os.environ["KZ_GAME"] = os.path.join(tmp, "no game here")
    try:
        r = launch.main(["--root", tmp, "--build-if-needed"])
    finally:
        sys.stderr = se
        os.environ.pop("KZ_GAME") if old_game is None else os.environ.__setitem__("KZ_GAME", old_game)
    check(r == launch.FILE_MISSING and "missing or still encrypted" in err.getvalue(),
          f"an empty folder, its game folder missing: the first run's check fails, and launch says so: {r} "
          f"{err.getvalue()}")
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

# --move-data: the saves follow the choice changed after the install, never over a file already there
with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    env = {"APPDATA": str(t / "appdata"), "KZ_WORK": str(t / "work")}
    run_folder = t / "work" / "vfs"
    ok, text = launch.move_data(tmp, "appdata", env)
    check(not ok and "setup program" in text, f"--move-data outside a setup install says so: {text}")
    (t / launch.INSTALL_INI).write_text("[install]\nportable=1\n")
    (run_folder / "gamedata").mkdir(parents=True)
    (run_folder / "kz1hd_pc.ini").write_text("mine")
    ok, text = launch.move_data(tmp, "appdata", env)
    data = t / "appdata" / "Project Vekta"
    check(ok and (data / "kz1hd_pc.ini").read_text() == "mine" and (data / "gamedata").is_dir()
          and not (run_folder / "kz1hd_pc.ini").exists(), f"portable to %APPDATA%: the saves moved: {text}")
    check(launch.data_dir(tmp, str(run_folder), env) == str(data), "the choice is recorded in vekta-install.ini")
    (run_folder / "kz1hd_pc.ini").write_text("another")
    ok, text = launch.move_data(tmp, "portable", env)
    check(not ok and "kz1hd_pc.ini" in text and (data / "kz1hd_pc.ini").read_text() == "mine"
          and launch.data_dir(tmp, str(run_folder), env) == str(data),
          f"a file on both sides: nothing moves, the choice stays: {text}")
    (run_folder / "kz1hd_pc.ini").unlink()
    ok, text = launch.move_data(tmp, "portable", env)
    check(ok and (run_folder / "kz1hd_pc.ini").read_text() == "mine"
          and launch.data_dir(tmp, str(run_folder), env) == str(run_folder), f"back to the install folder: {text}")
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
    check(r.returncode == 0 and set(sw) == {"GAME", "EBOOT", "PSARC"}, f"three switches: {sw}")
    env = dict(os.environ, KZ_GAME=sw["GAME"], KZ_EBOOT=sw["EBOOT"], KZ_PSARC=sw["PSARC"])
    q = subprocess.run([sys.executable, str(ROOT / "tools/check-game-files.py"), "--quick"], capture_output=True,
                       text=True, env=env)
    for label in ("PARAM.SFO", "EBOOT.elf"):
        check(f"DIFFERENT {label}" in q.stdout, f"{label}: the right kind and size, another hash: {q.stdout}")
    check("ok       data_ps3.psarc" in q.stdout and q.returncode == 1, "the archive passes --quick; the check fails")
    # Without a decrypted archive the game folder's own .edat is checked instead (patch 4014)
    q = subprocess.run([sys.executable, str(ROOT / "tools/check-game-files.py"), "--quick"], capture_output=True,
                       text=True, env=dict(env, KZ_PSARC=os.path.join(tmp, "none.psarc")))
    check("MISSING  data_ps3.psarc.edat (required)" in q.stdout and "data_ps3.psarc (" not in q.stdout,
          f"no archive: the game's .edat is required instead: {q.stdout}")
    usr = os.path.join(sw["GAME"], "USRDIR")
    os.makedirs(usr, exist_ok=True)
    with open(os.path.join(usr, "data_ps3.psarc.edat"), "wb") as f:
        f.write(b"NPD\0")
        f.truncate(3153321376)
    q = subprocess.run([sys.executable, str(ROOT / "tools/check-game-files.py"), "--quick"], capture_output=True,
                       text=True, env=dict(env, KZ_PSARC=os.path.join(tmp, "none.psarc")))
    check("ok       data_ps3.psarc.edat" in q.stdout, f"the game's .edat of the tested size passes --quick: {q.stdout}")

# The CPU question before the first page: the launcher (a dontcopy entry, first in [Files] so unpacking it reads
# nothing before it) runs --cpu-check in InitializeSetup against the level tools/kzbuild.py builds for by default
iss_text = (ROOT / "installer" / "vekta.iss").read_text()
files = iss_text.split("[Files]", 1)[1].split("\n[", 1)[0]
first = next(ln for ln in files.splitlines() if ln.startswith("Source:"))
check(first.startswith('Source: "{#Payload}\\{#LauncherExe}"') and "dontcopy" in first,
      f"the launcher is the first [Files] entry, dontcopy: {first}")
check(re.search(r"function InitializeSetup: Boolean;.*?ExtractTemporaryFile\('\{#LauncherExe\}'\).*?'--cpu-check'.*?"
                r"\(Level < \{#CpuLevel\}\)", iss_text, re.S) is not None,
      "InitializeSetup extracts the launcher and compares its --cpu-check level with CpuLevel")
sys.path.insert(0, str(ROOT / "tools"))
import kzbuild  # noqa: E402
m = re.search(r'#define CpuLevel "(\d)"', iss_text)
check(m is not None and int(m.group(1)) == kzbuild.cpu_required(kzbuild.march({})),
      f"the setup's CpuLevel is the level of tools/kzbuild.py's default KZ_MARCH: {m and m.group(1)}")
lc = (ROOT / "installer" / "launcher" / "launcher.c").read_text()
check('#include "../../port/kz1hd/cpu_level.h"' in lc and 'L"--cpu-check"' in lc and "return kz_cpu_level();" in lc,
      "the launcher's --cpu-check exits with port/kz1hd/cpu_level.h's level")

print("test_setup:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
