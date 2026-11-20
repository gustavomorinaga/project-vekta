#!/bin/sh
# Project Vekta, Linux release folder (scripts/run.sh): the first run, with its questions. Asks where your own files are (docs/GETTING-FILES.md), checks
# them, then lifts the game's code and compiles the port here with the bundled toolchain (Zig, CMake, Ninja:
# KZ_LINUX_TOOLCHAIN=pinned), with one progress bar (tools/release/firstrun.py; its options pass through, e.g.
# --jobs 4). At the end it offers an applications-menu entry. Run it again after moving your files; a stopped run
# resumes. Nothing is downloaded. NVIDIA DLSS is not offered here (its Linux library does not link with the bundled
# toolchain; tools/install.sh with the system's compilers has it): this folder builds with AMD FSR 3.1. Needs Python 3.10 or newer (ps3recomp's tools); the game then needs a
# Vulkan driver and the system's SDL2 (libSDL2-2.0.so.0). The log is in work/logs/.
here=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null ||
  { echo "run.sh: Python 3.10 or newer is needed (python3): install it from your distribution" >&2; exit 2; }
[ -d "$here/work/toolchain-cache/zig" ] || {
  echo "run.sh: the bundled toolchain is missing ($here/work/toolchain-cache): unpack the whole release again" >&2
  exit 2; }
export KZ_LINUX_TOOLCHAIN=pinned
exec python3 "$here/tools/release/firstrun.py" --interactive --bar --root "$here" "$@"
