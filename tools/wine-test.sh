#!/usr/bin/env bash
# Runs the Windows build under Wine or Proton for a while, inside tools/sandbox.sh, and says whether it ran: the
# game booted and kept presenting frames. No window by default (the Vulkan renderer draws offscreen) and no sound.
#   tools/wine-test.sh [--seconds N] [--min-fps F] [--proton DIR] [--window] [EXE]
#     EXE          default $KZ_WORK/build-win/KZ1HD.exe (tools/build-win.sh)
#     --seconds    how long it runs (default 60); the verdict reads the last 20 s
#     --min-fps    the frame rate it must hold over those 20 s (default 20)
#     --proton DIR a Proton folder (the one holding the `proton` script) instead of Wine (KZ_WINE, default wine)
#     --window     a window on your display instead of none, to look at it
# Its own prefix in $KZ_WORK/wine/ (your ~/.wine is not touched). Logs there too: <name>.log is the game's (KZ_LOG,
# which also works under Proton, where the exe's stderr does not reach this script), <name>-wine.log Wine's.
# Exit 0: ran; 1: did not boot, crashed or stalled (the log's tail is printed); 2: usage or setup.
# (After Project Helghast's tools/proton-test.sh, MIT: the sandbox, the prefix of its own, audio off by DLL overrides.)
set -uo pipefail
KZ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$KZ_ROOT/tools/env.sh"
SECS=60; MIN_FPS=20; PROTON=""; WINDOW=0; EXE=""
while (( $# )); do
  case "$1" in
    --seconds) SECS="${2:-}"; shift ;;
    --min-fps) MIN_FPS="${2:-}"; shift ;;
    --proton) PROTON="${2:-}"; shift ;;
    --window) WINDOW=1 ;;
    -*) echo "usage: tools/wine-test.sh [--seconds N] [--min-fps F] [--proton DIR] [--window] [EXE]" >&2; exit 2 ;;
    *) EXE="$1" ;;
  esac
  shift
done
[[ "$SECS" =~ ^[0-9]+$ ]] && (( SECS >= 30 )) || { echo "wine-test: --seconds needs 30 or more" >&2; exit 2; }
[[ "$MIN_FPS" =~ ^[0-9]+([.][0-9]+)?$ ]] || { echo "wine-test: --min-fps needs a number" >&2; exit 2; }
EXE="$(readlink -f "${EXE:-$KZ_WORK/build-win/KZ1HD.exe}")"
[[ -f "$EXE" ]] || { echo "wine-test: no $EXE (tools/build-win.sh)" >&2; exit 2; }
[[ -f "$KZ_VFS/PS3_GAME/USRDIR/EBOOT.elf" ]] || { echo "wine-test: no game in $KZ_VFS (tools/setup-vfs.sh)" >&2; exit 2; }
if [[ -n "$PROTON" ]]; then
  PROTON="$(readlink -f "$PROTON")"
  [[ -x "$PROTON/proton" ]] || { echo "wine-test: no proton script in $PROTON" >&2; exit 2; }
  runner=("$PROTON/proton" run); name=proton
else
  runner=("${KZ_WINE:-wine}"); name=wine
  command -v "${runner[0]}" >/dev/null || { echo "wine-test: ${runner[0]} not found (KZ_WINE)" >&2; exit 2; }
fi
D="$KZ_WORK/wine"; LOG="$D/$name.log"; WLOG="$D/$name-wine.log"
mkdir -p "$D/prefix" "$D/proton-compat" "$D/proton-steam"

# Inside the sandbox: only $KZ_WORK is writable, so the prefix, Proton's data and the log all live there.
env=(WINEPREFIX="$D/prefix" STEAM_COMPAT_DATA_PATH="$D/proton-compat" STEAM_COMPAT_CLIENT_INSTALL_PATH="$D/proton-steam"
     WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES="winepulse.drv,winealsa.drv,wineoss.drv=d;winemenubuilder.exe=d;mscoree,mshtml="
     KZ_LOG="Z:$LOG" KZ_STATS=1 PS3_VERBOSE=0 GCM_PARK_SKIP="${GCM_PARK_SKIP:-1}"
     PS3RECOMP_VK_GUEST_PROGRAMS="${PS3RECOMP_VK_GUEST_PROGRAMS:-1}"
     KZ_LIBSRE_IMAGE="${KZ_LIBSRE_IMAGE:-$KZ_WORK/fw/out/libsre_image.bin}")
(( WINDOW )) && env+=(PS3RECOMP_VK_WINDOW=1)
echo "wine-test: $EXE under ${runner[*]} for ${SECS}s ($([[ $WINDOW == 1 ]] && echo window || echo no window)); log $LOG"
rm -f "$LOG"
cd "$KZ_VFS" || exit 2
# A new prefix is made first, on its own clock, so its setup does not eat into the run.
reg="$D/prefix/system.reg"; [[ -n "$PROTON" ]] && reg="$D/proton-compat/pfx/system.reg"
if [[ ! -f "$reg" ]]; then
  echo "wine-test: making the prefix (once)"
  "$KZ_ROOT/tools/sandbox.sh" -- env "${env[@]}" timeout 600 "${runner[@]}" wineboot -u > "$D/$name-prefix.log" 2>&1 ||
    { echo "wine-test: the prefix could not be made; see $D/$name-prefix.log" >&2; exit 2; }
fi
"$KZ_ROOT/tools/sandbox.sh" --gpu -- env "${env[@]}" \
  timeout -s KILL "$SECS" "${runner[@]}" "$EXE" "Z:$PWD/PS3_GAME/USRDIR/EBOOT.elf" > "$WLOG" 2>&1
rc=$?
# timeout's 137 is the expected end (still running); anything else before the time is up is an exit or a crash. The
# sandbox has its own PID namespace, so wineserver and anything else Wine started end with it.

verdict="$(awk -v min="$MIN_FPS" -v secs="$SECS" '
  /\[kz-stats\]/ { match($0, /t=[0-9]+/); t = substr($0, RSTART + 2, RLENGTH - 2) + 0
                   match($0, /flips=[0-9]+/); f = substr($0, RSTART + 6, RLENGTH - 6) + 0
                   T[n] = t; F[n++] = f }
  END { if (n < 2) { print "FAIL no [kz-stats] lines: the game did not boot"; exit }
        for (i = n - 1; i > 0 && T[n-1] - T[i] < 20; i--) ;
        if (T[n-1] - T[i] < 10) { print "FAIL it stopped after " T[n-1] - T[0] " s of stats"; exit }
        fps = (F[n-1] - F[i]) / (T[n-1] - T[i])
        if (T[n-1] - T[0] < secs - 15) { printf "FAIL stats stopped after %d s (of %d)\n", T[n-1] - T[0], secs; exit }
        printf "%s %.1f fps over the last %d s (at least %s), %d frames in all\n", (fps >= min ? "PASS" : "FAIL"),
               fps, T[n-1] - T[i], min, F[n-1] }' "$LOG" 2>/dev/null)"
if [[ $rc != 137 && $rc != 0 ]]; then verdict="FAIL exited with status $rc before ${SECS}s"; fi
if cat "$LOG" "$WLOG" 2>/dev/null | grep -aqiE 'unhandled exception|unhandled page fault'; then verdict="FAIL a crash in the log"; fi
echo "wine-test: $verdict"
[[ "$verdict" == PASS* ]] && exit 0
echo "--- the game's log (tail) ---"; grep -av 'kz-stats' "$LOG" 2>/dev/null | tail -n 20
echo "--- Wine's log (tail) ---"; tail -n 10 "$WLOG"
exit 1
