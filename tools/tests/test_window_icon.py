#!/usr/bin/env python3
"""Host test for the game window's icon (ps3recomp patch 2081): port/kz1hd/window_icon.h is what tools/icon_header.py
makes from installer/art/vekta.ico (64x64 RGBA, opaque in the middle), main.cpp hands it and the app id
"project-vekta" to the backend, and the Linux menu entry names the same window class. No game data."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "release"))
import firstrun  # noqa: E402
import icon_header  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


r = subprocess.run([sys.executable, str(ROOT / "tools" / "icon_header.py"), "--check"], capture_output=True, text=True)
check(r.returncode == 0, f"window_icon.h is current: {r.stdout}")
rgba = icon_header.rgba_from_ico((ROOT / "installer" / "art" / "vekta.ico").read_bytes())
check(len(rgba) == 64 * 64 * 4, f"64x64 RGBA: {len(rgba)} bytes")
mid = (32 * 64 + 32) * 4
check(rgba[mid + 3] == 255, "the middle of the icon is opaque (BGRA turned into RGBA, rows not left as alpha 0)")
main = (ROOT / "port" / "kz1hd" / "main.cpp").read_text()
check('rsx_vulkan_set_window_identity("project-vekta"' in main and '#include "window_icon.h"' in main,
      "main.cpp gives the backend the icon and the app id")
entry = firstrun.desktop_entry("/opt/vekta")
check("StartupWMClass=project-vekta" in entry, "the Linux menu entry names the window's class")
check(firstrun.desktop_path().endswith("project-vekta.desktop"), "and is named after the app id (Wayland's match)")
rc = (ROOT / "port" / "kz1hd" / "app.rc").read_text()
check('1 ICON "../../installer/art/vekta.ico"' in rc and (ROOT / "port" / "kz1hd" / "../../installer/art/vekta.ico")
      .resolve().is_file(), "KZ1HD.exe gets vekta.ico as its icon (app.rc)")
import assemble_payload  # noqa: E402
check(any(r[0] == "installer/art/vekta.ico" for r in assemble_payload.RULES),
      "the Windows payload ships the .ico the player's build needs for app.rc")
series = (ROOT / "port" / "patches" / "series").read_text()
check(any(line.startswith("2081-") for line in series.splitlines()), "patch 2081 is in the series")

print("test_window_icon:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
