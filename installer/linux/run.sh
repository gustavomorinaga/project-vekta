#!/bin/sh
# Project Vekta, Linux release folder: the first run. Asks where your own files are (docs/GETTING-FILES.md), checks
# them, then lifts the game's code and compiles the port here with the bundled toolchain (Zig, CMake, Ninja:
# KZ_LINUX_TOOLCHAIN=pinned), with one progress bar (tools/release/firstrun.py; its options pass through, e.g.
# --jobs 4). At the end it offers an applications-menu entry. Run it again after moving your files; a stopped run
# resumes. Nothing is downloaded. Needs Python 3.8 or newer; the game then needs a Vulkan driver and the system's
# SDL2 (libSDL2-2.0.so.0). The log is in work/logs/.
here=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
command -v python3 >/dev/null 2>&1 || { echo "run.sh: python3 not found: install Python 3 (3.8 or newer)" >&2; exit 2; }
[ -d "$here/work/toolchain-cache/zig" ] || {
  echo "run.sh: the bundled toolchain is missing ($here/work/toolchain-cache): unpack the whole release again" >&2
  exit 2; }
export KZ_LINUX_TOOLCHAIN=pinned
exec python3 "$here/tools/release/firstrun.py" --interactive --bar --root "$here" "$@"
