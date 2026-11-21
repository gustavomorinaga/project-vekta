#!/bin/sh
# Project Vekta, Linux release folder (scripts/prepare.sh): checks your files and builds without a window or a question
# (tools/release/launch.py --prepare), for a launcher. Prints "progress <0..1> <text>" lines and, on a failure, one
# "reason <text>" line; exit 0 when built, else a code of port/kz1hd/kz_exit.h. Its options pass through: --game DIR,
# --eboot FILE, --psarc FILE, --rpcs3 [DIR], --timeout SECS. Needs Python 3.10 or newer.
here=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
cd "$here" || exit 1
python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null ||
  { echo "reason Python 3.10 or newer is needed (python3): install it from your distribution"; exit 1; }
exec python3 "$here/tools/release/launch.py" --root "$here" --prepare "$@"
