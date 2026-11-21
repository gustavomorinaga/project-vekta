#!/usr/bin/env python3
"""Host test for KZ1HD.exe's resources and version (port/kz1hd/app.rc, app.manifest, version.cmake): app.rc has the
icon, the version block and the manifest; the manifest is well-formed XML naming Windows 10/11 and the DPI awareness
win_dpi.h asks for; no game or publisher name is in either; version.cmake's kz_version.h starts with the same two
lines tools/release/build_launcher.py writes for the launcher, from the same VERSION; CMakeLists.txt states no version
of its own; main.cpp prints the version line for --version and as the log's first line; the payload carries what a
player's Windows build needs for it. The Windows side (the resources read back by GetFileVersionInfoW under Wine) is
tools/tests/test_exe_resources.c in tools/wine-host-tests.sh. No game data, no network."""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
K = ROOT / "port" / "kz1hd"
sys.path.insert(0, str(ROOT / "tools" / "release"))
import assemble_payload  # noqa: E402
import build_launcher  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


rc = (K / "app.rc").read_text()
check(re.search(r'^1 ICON "\.\./\.\./installer/art/vekta\.ico"', rc, re.M) is not None, "app.rc: the icon")
check(re.search(r"^1 VERSIONINFO", rc, re.M) is not None and '#include "kz_version.h"' in rc
      and "FILEVERSION VEKTA_VER_COMMA" in rc, "app.rc: the version block, from kz_version.h")
check(re.search(r'^1 24 "app\.manifest"', rc, re.M) is not None, "app.rc: the manifest (resource 1, RT_MANIFEST)")
for key, want in (("CompanyName", "Project Vekta contributors"), ("FileDescription", "Project Vekta"),
                  ("ProductName", "Project Vekta"), ("OriginalFilename", "KZ1HD.exe")):
    check(f'VALUE "{key}", "{want}"' in rc, f"app.rc: {key} is {want}")

man_text = (K / "app.manifest").read_text()
try:
    man = ET.fromstring(man_text.encode())
except ET.ParseError as err:
    man = None
    check(False, f"app.manifest is well-formed XML: {err}")
if man is not None:
    ids = [e.get("Id") for e in man.iter("{urn:schemas-microsoft-com:compatibility.v1}supportedOS")]
    check(ids == ["{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"], f"app.manifest: supportedOS Windows 10/11 only: {ids}")
    dpi = [e.text for e in man.iter("{http://schemas.microsoft.com/SMI/2016/WindowsSettings}dpiAwareness")]
    hint = re.search(r'_putenv_s\(KZ_DPI_HINT, "(\w+)"\)', (K / "win_dpi.h").read_text())
    check(len(dpi) == 1 and hint is not None and dpi[0].lower() == hint.group(1),
          f"app.manifest: dpiAwareness is win_dpi.h's value: {dpi}")
GAME = re.compile(r"killzone|guerrilla|sony|playstation|npua|ps3\b", re.I)
strings = re.findall(r'"([^"]*)"', rc) + re.findall(r'(?:name|Id)="([^"]*)"', man_text)
check(not [s for s in strings if GAME.search(s)], "no game or publisher name in app.rc's or app.manifest's strings")

cm = (K / "CMakeLists.txt").read_text()
proj = re.search(r"project\(KZ1HD(.*?)\)", cm, re.S)
check(proj is not None and "VERSION" not in proj.group(1), "CMakeLists.txt: project() states no version of its own")
check('include("${CMAKE_CURRENT_SOURCE_DIR}/version.cmake")' in cm, "CMakeLists.txt includes version.cmake")
main = (K / "main.cpp").read_text()
check('strcmp(argv[1], "--version")' in main and '"[kz] %s\\n", KZ_VERSION_LINE' in main,
      "main.cpp: --version, and the version line opens the log")

cmake = shutil.which("cmake")
check(cmake is not None, "cmake is installed")
if cmake:
    with tempfile.TemporaryDirectory() as t:
        t = pathlib.Path(t)

        def gen(version, patchset=None):
            (t / "VERSION").write_text(version + "\n")
            ps = t / ".patchset"
            ps.unlink(missing_ok=True)
            if patchset:
                ps.write_text(patchset + "\n")
            out = t / "kz_version.h"
            out.unlink(missing_ok=True)
            r = subprocess.run([cmake, f"-DKZ_VERSION_FILE={t / 'VERSION'}", f"-DKZ_PATCHSET_FILE={ps}",
                                f"-DKZ_VERSION_OUT={out}", "-P", str(K / "version.cmake")],
                               capture_output=True, text=True)
            return r.returncode, out.read_text() if out.exists() else ""

        code, h = gen("1.2.3", "0123456789abcdef-ae92407b")
        check(code == 0 and h.startswith(build_launcher.version_header("1.2.3")),
              f"kz_version.h starts with the launcher's two lines for the same VERSION:\n{h}")
        check('#define KZ_VERSION_LINE "Project Vekta 1.2.3 (patch set 0123456789abcdef, ps3recomp ae92407b)"' in h,
              "the version line names the patch set and ps3recomp's commit")
        code, h = gen("1.2.3")
        check(code == 0 and "patch set unknown, ps3recomp unknown" in h, "no .patchset: both unknown")
        code, h = gen("1.2")
        check(code != 0, "a VERSION that is not MAJOR.MINOR.PATCH stops the configure")

rules = {r[0]: r for r in assemble_payload.RULES}
kz = rules.get("port/kz1hd")
check(kz is not None and kz[2] == "tree" and not kz[3].get("include")
      and not {"app.manifest", "version.cmake"} & set(kz[3].get("exclude", [])),
      "the payload carries port/kz1hd with app.rc, app.manifest and version.cmake")
tk = rules.get(assemble_payload.TOOLKIT)
check("VERSION" in rules and tk is not None and ".patchset" not in tk[3].get("exclude", []),
      "and VERSION and the patched toolkit's .patchset that version.cmake reads")
print("test_exe_resources:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
