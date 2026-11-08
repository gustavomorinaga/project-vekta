#!/bin/sh
# In-game check of patch 2059 (needs your game files, a GPU and a display; not a host test). From a fresh profile on
# a copy of the run folder, scripted input reaches Level 1, opens the pause menu's Debug Options (KZ_DEBUG_MENU=1),
# sets Next Level and resumes, which ends the level on the VICTORY screen; then it continues into Level 2 and does
# the same again, CYCLES times in all; at the end it quits from the pause menu to the main menu. It checks that the
# game's copy of the frame into a screen's background texture ran (a local-to-local "[NV0039]" line in the log),
# that no menu background in the presented frames is grainy (the mean difference between neighbouring pixels: 7.4
# in a report's screenshot, under 0.2 for the flat VICTORY and the tinted pause snapshot), and that the main menu
# after quitting is visible (not dark).
# Usage: tools/menu-bg-check.sh [CYCLES] (default 5, which plays more than 8 movies: patch 3033). Close the game first: one instance at a time.
# KZ_BIN: another build (default work/build/KZ1HD).
set -eu
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
cycles=${1:-5}   # 5 cycles play more than 8 movies (patch 3033: the 8th used to fail)
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
    t=$((t + 130)); i=$((i + 1))
done
# pause, DOWN x5 to Quit, X, X (confirm): the main menu
script="$script,$t:0x0008"
for d in 4 6 8 10 12; do script="$script,$((t + d)):0x0040"; done
script="$script,$((t + 15)):0x4000,$((t + 19)):0x4000"
quit_at=$((t + 19)); t=$((t + 50))
PS3RECOMP_VK_WINDOW=1 PS3_GCM_MAX_QUEUED_FLIPS=1 PS3_VERBOSE=0 KZ_DEBUG_MENU=1 KZ_VFS="$out/vfs" KZ_BIN="$bin" \
    PS3RECOMP_VK_DUMP="$out/frames/g%06u.ppm" PS3RECOMP_VK_DUMP_PRESENT="$out/frames/p%06u.ppm" PS3RECOMP_VK_DUMP_MS=3000 \
    PAD_SCRIPT="$script" "$KZ_ROOT/tools/run.sh" "$((t + 10))" "$out/run.log"
rm -f "$out"/frames/g*.ppm
copies=$(grep -ac '^\[NV0039\] 0:0x[0-9A-F]* -> 0:0x' "$out/run.log" || true)
echo "local-to-local copies logged: $copies"
# the main menu's movie opened after the last level started: the quit reached the main menu
menu_movie=$(awk '/kz-sail\] open .*Movies\/Level/ { n = 0 } /kz-sail\] open .*Campaign\.avi/ { n++ } END { print n + 0 }' "$out/run.log")
echo "main menu movie opened after the last level: $menu_movie"
# patch 3033: no movie's audio registration may fail, and no join may take a thread id that is not one
notify_full=$(grep -ac 'notify slots in use' "$out/run.log" || true)
bad_join=$(grep -aoE 'ppu_thread_join\(tid=[0-9]+\)' "$out/run.log" | tr -dc '0-9\n' | awk '$1 > 64' | wc -l)
movies=$(grep -ac 'kz-sail\] open ' "$out/run.log" || true)
echo "movies opened: $movies; notify slots full: $notify_full; joins of a bad thread id: $bad_join"
# a Python with Pillow (the build venv, which env.sh puts first, may not have it)
py=
for c in "$KZ_PY" python3 /usr/bin/python3; do
    if "$c" -c 'import PIL' >/dev/null 2>&1; then py=$c; break; fi
done
[ -n "$py" ] || { echo "menu-bg-check: no Python with Pillow (pip install pillow)" >&2; exit 1; }
"$py" - "$out/frames" "$copies" "$(( (quit_at + 15) * 1000 ))" "$menu_movie" "$notify_full" "$bad_join" <<'PY'
import glob, os, statistics, sys
from PIL import Image
frames, copies, menu_ms, menu_movie = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
notify_full, bad_join = int(sys.argv[5]), int(sys.argv[6])
def grain(c):
    """mean difference between horizontal neighbours, on every other pixel"""
    p = c.load(); W, H = c.size
    d = [abs(p[x, y] - p[x + 1, y]) for y in range(0, H, 2) for x in range(0, W - 1, 2)]
    return sum(d) / len(d)
menus, main_menu = [], []
fs = sorted(glob.glob(frames + "/p*.ppm"), key=os.path.getmtime)
t0 = os.path.getmtime(fs[0]) if fs else 0
for f in fs:
    im = Image.open(f).convert("RGB")
    w, h = im.size
    # the flat area above a menu's items; the screens' palette tints it green (G above R and B)
    c = im.crop((int(w * 0.3), int(h * 0.12), int(w * 0.45), int(h * 0.2)))
    r, g, b = (statistics.mean(ch.getdata()) for ch in c.split())
    after_quit = (os.path.getmtime(f) - t0) * 1000 >= menu_ms - 12000
    if 3 < g - r < 20 and 3 < g - b < 20 and not after_quit:   # (the main menu's movie is green too)
        menus.append((f.rsplit("/", 1)[1], round(grain(c.convert("L")), 2)))
    if after_quit:
        L = list(im.crop((w // 4, h // 8, w * 3 // 4, h * 7 // 8)).convert("L").getdata())
        main_menu.append((f.rsplit("/", 1)[1], round(statistics.mean(L), 1), round(statistics.pstdev(L), 1)))
for m in menus:
    print("%s menu background grain %s" % m)
for m in main_menu:
    print("%s after quitting: mean %s std %s" % m)
fail = []
if not menus:
    fail.append("no menu frame found in the dumps (did the script reach VICTORY?)")
if copies == 0:
    fail.append("no local-to-local NV0039 copy: the screens' background was not copied")
grainy = [m[0] for m in menus if m[1] > 2.0]
if grainy:
    fail.append("a grainy background in " + ", ".join(grainy))
if notify_full or bad_join:
    fail.append("a movie's audio could not start (notify slots full: %d, bad thread joins: %d)" % (notify_full, bad_join))
if menu_movie == 0:
    fail.append("the quit did not reach the main menu (its movie never opened after the last level)")
if not main_menu:
    fail.append("no frame after quitting to the main menu")
elif all(m[1] < 12 for m in main_menu[-3:]):
    fail.append("the main menu is dark after quitting")
if fail:
    sys.exit("FAIL: " + "; ".join(fail))
print("menu-bg-check: ok")
PY
