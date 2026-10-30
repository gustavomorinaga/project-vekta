#!/bin/sh
# Runs the Killzone 1 HD port from its VFS for SECS seconds (default 60), log in LOG
# without the per-frame spam. SIGTERM quits cleanly; the timeout still sends SIGKILL.
# Usage: tools/run.sh [SECS] [LOG]
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
secs=${1:-60}; log=$(readlink -m "${2:-$KZ_WORK/run.log}")
bin="${KZ_BIN:-$KZ_WORK/build/KZ1HD}"
cd "$KZ_VFS" || exit 1   # KZ_VFS: another VFS root (tests keep their own saves)
GCM_PARK_SKIP=${GCM_PARK_SKIP:-1} PS3RECOMP_VK_GUEST_PROGRAMS=${PS3RECOMP_VK_GUEST_PROGRAMS:-1} KZ_LIBSRE_IMAGE=${KZ_LIBSRE_IMAGE:-$KZ_WORK/fw/out/libsre_image.bin} \
  timeout -s KILL "$secs" "$bin" "$PWD/PS3_GAME/USRDIR/EBOOT.elf" 2>&1 |
  grep -av -e 'event_queue_receive' -e '_SendSignal' -e 'WAIT_SIGNAL#' -e 'signal task' -e 'semaphore_wait' > "$log"
echo "log: $log ($(wc -l < "$log") lines)"
