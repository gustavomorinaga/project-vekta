#!/usr/bin/env python3
"""Host test for the Windows setup program's pieces: installer/vekta.iss names files the payload has (its LICENSE,
VERSION and the launcher) and its art; the launcher (installer/launcher/launcher.c) starts the Python and the script
at the paths assemble_payload.py gives them, and passes --build-if-needed, which launch.py takes; the launcher's
version block comes from VERSION; launch.py --build-if-needed without run.cmd says what is missing; the Inno Setup
pin is there; .cmd and .iss files keep CRLF. No compiler and no Wine: tools/release/build_launcher.py and
build_setup.py run on the maintainer's machine (docs/BUILDING.md)."""
import io
import json
import pathlib
import re
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
    check(ref == "*" or ref == "VERSION" or ref == "LICENSE" or (ROOT / ref).exists(),
          f"vekta.iss names a payload file the payload has: {ref}")
check('#define LauncherExe "Project Vekta.exe"' in text, "vekta.iss starts the launcher assemble_payload ships")
check((ROOT / "installer" / "art" / "vekta.ico").is_file() and "SetupIconFile=art\\vekta.ico" in text,
      "the setup icon is there")
check("PrivilegesRequired=lowest" in text and "{localappdata}\\Programs\\ProjectVekta" in text,
      "a per-user install, by default into a folder without spaces")
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

print("test_setup:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
