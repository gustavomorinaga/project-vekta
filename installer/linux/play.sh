#!/bin/sh
# Project Vekta, Linux release folder (scripts/play.sh): plays the port run.sh built (tools/play.sh). KZ_FULLSCREEN=1 for full screen;
# the settings are in work/vfs/PS3_GAME/USRDIR/kz1hd_pc.ini, the log in work/play.log.
here=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
[ -x "$here/work/build/KZ1HD" ] || { echo "play.sh: not built yet: run ./project-vekta or scripts/run.sh first" >&2; exit 1; }
exec "$here/tools/play.sh" "$@"
