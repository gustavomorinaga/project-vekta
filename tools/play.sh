#!/bin/sh
# Plays the Killzone 1 HD port in a window (KZ_FULLSCREEN=1 for full screen).
# Keyboard and mouse: WASD move, mouse aims (click in the window to capture it),
# left button fires, right zooms, middle = secondary fire, wheel = sniper zoom,
# E action, Q weapons, R reload, G grenade, Ctrl crouch, Shift sprint, X heat
# vision, Tab objectives, Esc pause. The layout is kz1hd_keys.ini in the VFS
# directory (written on the first run; edit it to remap, delete it for the
# defaults); mouse sensitivity is in Pause > Controls > Advanced Settings. In
# menus the mouse points and clicks. A controller works too. Closing the window
# stops the game.
# Log in $KZ_WORK/play.log.
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
bin="${KZ_BIN:-$KZ_WORK/build/KZ1HD}"   # KZ_BIN: another build (work/build-win/KZ1HD.exe)
cd "$KZ_VFS" || exit 1
export GCM_PARK_SKIP=${GCM_PARK_SKIP:-1} PS3RECOMP_VK_GUEST_PROGRAMS=${PS3RECOMP_VK_GUEST_PROGRAMS:-1}
# The per-wait diagnostics only cost time and disk in play (~100 MB per session).
export PS3_VERBOSE=${PS3_VERBOSE:-0} KZ_STATS=${KZ_STATS:-1}
export KZ_LIBSRE_IMAGE=${KZ_LIBSRE_IMAGE:-$KZ_WORK/fw/out/libsre_image.bin}
export PS3RECOMP_VK_WINDOW=1 PS3_TITLE="Killzone HD"
# Above 30 fps: the game clock at 60 Hz (KZ_TICK_HZ=30 is the original) and
# the game's own vblank at the PS3's 59.94, a steady 60 fps; flips shown on a
# 144 Hz cadence. fps_limit = 144 in kz1hd_pc.ini: a 144 Hz clock with flips
# let through as soon as they are ready (KZ_VBLANK_HZ=1000), ~90-115 fps in
# Level 1, limited by the PPU.
# The game runs at most one frame ahead of the screen (2 adds ~16 ms of input lag).
export PS3_GCM_MAX_QUEUED_FLIPS=${PS3_GCM_MAX_QUEUED_FLIPS:-1}
# Display mode, resolution, V-Sync and the frame-rate limit come from
# kz1hd_pc.ini in the VFS directory (written with defaults on the first run;
# see port/kz1hd/pcoptions.cpp); environment variables still override it.
[ "${KZ_FULLSCREEN:-0}" = 1 ] && export PS3RECOMP_VK_DISPLAY=borderless
# KZ_MANGOHUD=1: MangoHUD's statistics over the game (also "overlay = mangohud"
# in kz1hd_pc.ini, or Performance Overlay in the PC screen; see pcoptions.cpp).
case "$bin" in
*.exe)  # the Windows build (build-win.sh), under Wine (KZ_WINE: another runner)
    WINEDEBUG=${WINEDEBUG:--all} ${KZ_WINE:-wine} "$bin" "Z:$PWD/PS3_GAME/USRDIR/EBOOT.elf" > "$KZ_WORK/play.log" 2>&1 & ;;
*)  "$bin" "$PWD/PS3_GAME/USRDIR/EBOOT.elf" > "$KZ_WORK/play.log" 2>&1 & ;;
esac
pid=$!
# The window closing leaves the process running headless: stop it then.
while kill -0 $pid 2>/dev/null; do
    grep -aq 'window closed\|window disabled' "$KZ_WORK/play.log" 2>/dev/null && break
    sleep 1
done
kill -9 $pid 2>/dev/null
echo "log: $KZ_WORK/play.log"
