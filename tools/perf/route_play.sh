#!/bin/bash
# Replays one recorded route (port/kz1hd/route.cpp) headless and leaves its log; used by tools/perf/ab.sh.
# Does not take the benchmark lock itself: the caller wraps it (ab.sh does).
#
#   tools/perf/route_play.sh <binary> <route> <level n> <out prefix>
#
# The start is a saved profile's level list, as kz-route's route.sh "pick:<save>:<n>": ROUTE_SAVE (a savedata folder
# NPUA80856, copied into the VFS's savedata before the run, replacing what is there), level n from the top, Normal.
# Environment: KZ_VFS (a test VFS, never the player's: its savedata is replaced; its kz1hd_pc.ini must be the one the
# route was recorded with, as the mouse look is replayed through mouse_invert/mouse_yaw/mouse_pitch and the FOV
# changes what the AI sees), ROUTE_SAVE, KZ_LIBSRE_IMAGE (default $KZ_WORK/fw/out/libsre_image.bin), WIN=1 (a window),
# EXTRA_ENV="VAR=value ..." (more variables for the game), TOP_AT (seconds into the replay for the snapshot of the
# other processes' CPU, default 40). The game runs as <binary dir>/KZ1HD-perf so that scripts which clean up with
# pkill -x KZ1HD leave it alone; only this script's own PID is ever killed.
set -u
bin=$(readlink -f "$1"); route=$(readlink -f "$2"); n=$3; out=$(readlink -m "$4")
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd); export KZ_ROOT
. "$KZ_ROOT/tools/env.sh"
V=$KZ_VFS
[ -n "${ROUTE_SAVE:-}" ] && [ -d "$ROUTE_SAVE" ] || { echo "ROUTE_SAVE: a savedata folder (NPUA80856)"; exit 1; }
[ "$(readlink -f "$V")" != "$(readlink -f "$KZ_WORK/vfs")" ] || { echo "KZ_VFS is the player's vfs: use a test copy"; exit 1; }
log=$out.log
pad=$(mktemp); trap 'rm -f "$pad"' EXIT
D=$V/gamedata/dev_hdd0/home/00000001/savedata
rm -rf "$D/NPUA80856"; mkdir -p "$D"; cp -a "$ROUTE_SAVE" "$D/NPUA80856"
keys="0x0008 0x4000 0x4000@4$(for _ in $(seq 12); do printf ' 0x0010_20@1'; done)$(for _ in $(seq 2 "$n"); do printf ' 0x0040_20@1'; done) 0x4000@5 0x0010_20@1 0x0010_20@1 0x0010_20@1 0x0040_20@1 0x4000@5 0x4000@1"
run=$(dirname "$bin")/KZ1HD-perf; cmp -s "$bin" "$run" || cp -f "$bin" "$run"
libdir="$(dirname "$bin")/lib"; [ -d "$libdir" ] && export LD_LIBRARY_PATH="$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$V" || exit 1
mkdir -p "$V/route/bin"; ln -sfn "$V/PS3_GAME/USRDIR/EBOOT.elf" "$V/route/bin/EBOOT.elf"
# shellcheck disable=SC2086
env KZ_ROUTE_PLAY="$route" KZ_ROUTE_EXIT=1 KZ_ROUTE_LOOSE=1 KZ_TICK_HZ=144 KZ_GUEST_VBLANK_HZ=144 \
  KZ_STATS=1 PAD_FILE="$pad" PS3_VERBOSE=0 AUDIO_RATE=1 PS3RECOMP_VK_WINDOW=${WIN:-0} \
  GCM_PARK_SKIP=1 PS3RECOMP_VK_GUEST_PROGRAMS=1 KZ_LIBSRE_IMAGE=${KZ_LIBSRE_IMAGE:-$KZ_WORK/fw/out/libsre_image.bin} \
  ${EXTRA_ENV:-} "$run" "$V/route/bin/EBOOT.elf" > "$log" 2>&1 &
pid=$!
echo "$pid" > "$out.pid"
for _ in $(seq 120); do grep -aq "descriptor 2:" "$log" 2>/dev/null && break; kill -0 $pid 2>/dev/null || break; sleep 1; done
sleep 5
for k in $keys; do case $k in *@*) w=${k#*@}; k=${k%@*} ;; *_*) w=2 ;; *) w=9 ;; esac; echo "${k/_/ }" > "$pad"; sleep "$w"; done
for _ in $(seq 180); do grep -aq "^\[route\] replaying" "$log" && break; kill -0 $pid 2>/dev/null || break; sleep 1; done
{ echo "replay_start $(date +%s)"; echo "loadavg_start $(cut -d' ' -f1-3 /proc/loadavg)"; } > "$out.env"
(   # every thread's CPU time once a second while the replay runs: "t <monotonic s>" then "<tid> <utime+stime ticks> <name>"
    : > "$out.cpu"
    while kill -0 $pid 2>/dev/null; do
        { echo "t $(cut -d' ' -f1 /proc/uptime)"
          for t in /proc/$pid/task/*; do
              s=$(cat "$t/stat" 2>/dev/null) || continue
              rest=${s##*) }
              read -r -a f <<< "$rest"
              echo "${t##*/} $(( f[11] + f[12] )) $(cat "$t/comm" 2>/dev/null)"
          done; } >> "$out.cpu"
        sleep 1
    done
) &
(   # the other processes' CPU over 3 s (top's second sample is an instant reading, not a lifetime average)
    sleep "${TOP_AT:-40}"; kill -0 $pid 2>/dev/null || exit 0
    echo "loadavg_mid $(cut -d' ' -f1-3 /proc/loadavg)" >> "$out.env"
    top -b -n 2 -d 3 -w 200 | awk '/^top -/{n++} n==2' | awk 'f && $9+0 >= 3 {print "top " $9 " " $12} /PID/{f=1}' |
        grep -v " KZ1HD-perf$" | head -6 >> "$out.env"
) &
for _ in $(seq "${ROUTE_MAX:-1800}"); do kill -0 $pid 2>/dev/null || break; sleep 1; done   # ROUTE_MAX: seconds a replay may take
kill -9 $pid 2>/dev/null
wait $pid 2>/dev/null
echo "loadavg_end $(cut -d' ' -f1-3 /proc/loadavg)" >> "$out.env"
grep -a "^\[route\] replay ended\|^\[perf\] replay: [0-9]" "$log"
