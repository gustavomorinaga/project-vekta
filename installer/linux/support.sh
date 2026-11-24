#!/bin/sh
# Project Vekta, Linux release folder (scripts/support.sh): makes the support package, one zip of the logs, the crash
# reports and the settings for a bug report (tools/release/launch.py --support-package, as ./project-vekta
# --support-package), in your home folder's Desktop (else the home folder); --out DIR puts it elsewhere. Your home
# folder is written as ~, and it holds no saves and no game files. Nothing is sent anywhere: you attach the zip.
here=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
cd "$here" || exit 1
python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null ||
  { echo "support.sh: Python 3.10 or newer is needed (python3): install it from your distribution" >&2; exit 1; }
exec python3 "$here/tools/release/launch.py" --root "$here" --support-package "$@"
