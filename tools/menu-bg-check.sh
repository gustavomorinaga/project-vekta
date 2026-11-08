#!/bin/sh
# In-game check of patch 2059 (needs your game files, a GPU and a display; not a host test). From a fresh profile on
# a copy of the run folder, scripted input reaches Level 1, opens the pause menu's Debug Options (KZ_DEBUG_MENU=1),
# sets Next Level and resumes, which ends the level on the VICTORY screen; then it continues into Level 2 and does
# the same again, CYCLES times in all. It checks that each time a screen opened the game's copy of the frame into
# the screen's background texture ran (a local-to-local "[NV0039]" line in the log) and that the VICTORY
# background in the presented frames stays flat (std 0) at the same brightness every time.
# Usage: tools/menu-bg-check.sh [CYCLES] (default 3). Close the game first: one instance at a time.
# KZ_BIN: another build (default work/build/KZ1HD).
set -eu
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
cycles=${1:-3}
bin=${KZ_BIN:-$KZ_WORK/build/KZ1HD}
if pgrep -x KZ1HD >/dev/null 2>&1; then echo "menu-bg-check: the game is running; close it first" >&2; exit 1; fi
out="$KZ_WORK/menu-bg-check"
rm -rf "$out"; mkdir -p "$out/frames"
cp -a "$KZ_VFS" "$out/vfs"
rm -rf "$out/vfs/gamedata/dev_hdd0/home/00000001/savedata/NPUA80856"
trap 'rm -rf "$out/vfs"' EXIT
# Fresh profile to Level 1 (docs/dev/testing.md), then per cycle: pause, Debug Options (UP from Resume), X, DOWN x6
# to Next Level, RIGHT (Yes), CIRCLE twice (resume), and X on VICTORY's Continue.
script="15:0x0008,25:0x4000,35:0x0008,45:0x4000,60:0x4000,66:0x4000,72:0x4000,78:0x4000,84:0x4000,90:0x4000"
t=96; i=0
while [ "$i" -lt "$cycles" ]; do
    script="$script,$t:0x0008,$((t + 4)):0x0010,$((t + 7)):0x4000"
    for d in 10 12 14 16 18 20; do script="$script,$((t + d)):0x0040"; done
    script="$script,$((t + 23)):0x0020,$((t + 26)):0x2000,$((t + 29)):0x2000,$((t + 50)):0x4000"
    t=$((t + 110)); i=$((i + 1))
done
PS3RECOMP_VK_WINDOW=1 PS3_GCM_MAX_QUEUED_FLIPS=1 PS3_VERBOSE=0 KZ_DEBUG_MENU=1 KZ_VFS="$out/vfs" KZ_BIN="$bin" \
    PS3RECOMP_VK_DUMP="$out/frames/g%06u.ppm" PS3RECOMP_VK_DUMP_PRESENT="$out/frames/p%06u.ppm" PS3RECOMP_VK_DUMP_MS=3000 \
    PAD_SCRIPT="$script" "$KZ_ROOT/tools/run.sh" "$((t + 10))" "$out/run.log"
rm -f "$out"/frames/g*.ppm
copies=$(grep -ac '^\[NV0039\] 0:0x[0-9A-F]* -> 0:0x' "$out/run.log" || true)
echo "local-to-local copies logged: $copies"
python3 - "$out/frames" "$copies" <<'PY'
import glob, statistics, sys
from PIL import Image
frames, copies = sys.argv[1], int(sys.argv[2])
seen = []
for f in sorted(glob.glob(frames + "/p*.ppm")):
    im = Image.open(f).convert("RGB")
    w, h = im.size
    # the flat area above the stats bars; the screens' palette tints it green (G above R and B)
    c = im.crop((int(w * 0.3), int(h * 0.12), int(w * 0.45), int(h * 0.2)))
    r, g, b = (statistics.mean(ch.getdata()) for ch in c.split())
    if 3 < g - r < 20 and 3 < g - b < 20:
        L = list(c.convert("L").getdata())
        seen.append((f.rsplit("/", 1)[1], round(statistics.mean(L), 1), round(statistics.pstdev(L), 2)))
for s in seen:
    print("%s background mean %s std %s" % s)
bad = [s for s in seen if s[2] > 1.0]
if not seen:
    sys.exit("FAIL: no menu frame found in the dumps (did the script reach VICTORY?)")
if copies == 0:
    sys.exit("FAIL: no local-to-local NV0039 copy: the screens' background was not copied")
if bad:
    sys.exit("FAIL: a grainy background in %s" % ", ".join(s[0] for s in bad))
print("menu-bg-check: ok")
PY
