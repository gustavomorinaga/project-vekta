#!/usr/bin/env python3
"""Build the Windows release's launcher, "Project Vekta.exe", from installer/launcher/launcher.c with the pinned
llvm-mingw (tools/release/toolchain.json; fetched into work/toolchain-cache when it is not there yet):

    tools/release/build_launcher.py [--out DIR] [--cache DIR]      (default work/release/launcher)

launcher.rc gives it the icon (installer/art/vekta.ico, from assets/icon.svg by tools/site-art.sh) and the version
block, written from VERSION into launcher_version.h. Plain Win32, GUI subsystem, Unicode entry, -Os and stripped, no
PE timestamp: the same sources and compiler give the same bytes. Runs on Linux (the cross llvm-mingw) and on
Windows (the Windows-hosted one). After Project Pyrrhus's and Project Helghast's tools/release/build_launcher.sh
(undecuple, MIT). Prints the exe's size and SHA-256. Exit 0 = built, 1 = failed. Standard library only.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import fetch_toolchain  # noqa: E402
import release_version  # noqa: E402

EXE = "Project Vekta.exe"
FLAGS = ["-Os", "-s", "-municode", "-mwindows", "-Wall", "-Wextra", "-Werror", "-Wl,--no-insert-timestamp"]


def version_header(version):
    parts = version.split(".") + ["0"]
    return (f"#define VEKTA_VER_COMMA {','.join(parts[:4])}\n"
            f'#define VEKTA_VER_STR "{version}"\n')


def main(argv=None):
    work = os.environ.get("KZ_WORK") or os.path.join(ROOT, "work")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=os.path.join(work, "release", "launcher"))
    ap.add_argument("--cache", default=os.path.join(work, "toolchain-cache"))
    a = ap.parse_args(argv)
    name = "llvm-mingw" if os.name == "nt" else "llvm-mingw-linux"
    try:
        tree = fetch_toolchain.fetch_one(next(e for e in fetch_toolchain.load()["components"] if e["name"] == name),
                                         a.cache)
        version = release_version.read(ROOT)
    except (ValueError, OSError) as err:
        print(f"build_launcher: {err}", file=sys.stderr)
        return 1
    exe_suffix = ".exe" if os.name == "nt" else ""
    cc = os.path.join(tree, "bin", "x86_64-w64-mingw32-clang" + exe_suffix)
    windres = os.path.join(tree, "bin", "x86_64-w64-mingw32-windres" + exe_suffix)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "launcher_version.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(version_header(version))
    res = os.path.join(a.out, "launcher.res.o")
    out = os.path.join(a.out, EXE)
    inst = os.path.join(ROOT, "installer")
    try:
        subprocess.run([windres, "--include-dir", os.path.join(inst, "art"), "--include-dir", a.out, "-O", "coff",
                        "-o", res, os.path.join(inst, "launcher", "launcher.rc")], check=True)
        subprocess.run([cc, *FLAGS, "-o", out, os.path.join(inst, "launcher", "launcher.c"), res], check=True)
    except (OSError, subprocess.CalledProcessError) as err:
        print(f"build_launcher: {err}", file=sys.stderr)
        return 1
    print(f"{os.path.getsize(out)} {out}")
    print(fetch_toolchain.sha256_file(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
