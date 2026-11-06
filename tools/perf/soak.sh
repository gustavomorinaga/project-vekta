#!/bin/bash
# Soak test: a recorded route replayed in a loop (KZ_ROUTE_LOOP, loose, God mode) for hours,
# headless, sampling the game's memory every few seconds.
#
#   tools/perf/soak.sh <route> <start> <hours> [out dir]
#
# <route> and <start> as in tools/route/route.sh play. Writes to <out dir> (default
# soak-<date> in the working directory):
#   game.log     the game's output: a [route] and [perf] summary for each loop
#   samples.csv  every SOAK_EVERY seconds (default 30): RSS, threads, fds, VRAM (nvidia-smi)
#   frames.txt   each loop's present intervals (KZ_PERF_OUT)
# and ends with tools/perf/frametimes.py soak <out dir>: memory growth per hour and fps per loop.
# Benchmarks must not overlap: run it under the shared lock, e.g.
#   flock <repo>/work/bench.lock tools/perf/soak.sh ...
set -u
route=$(readlink -f "$1"); start=$2; hours=$3
here=$(cd "$(dirname "$0")/../.." && pwd)
out=$(readlink -f "${4:-soak-$(date +%Y%m%d-%H%M)}")
every=${SOAK_EVERY:-30}
mkdir -p "$out"; : > "$out/frames.txt"
pidfile=$out/pid; rm -f "$pidfile"
ROUTE_LOG=$out/game.log ROUTE_PIDFILE=$pidfile ROUTE_MAX=$(awk "BEGIN{printf \"%d\", $hours * 3600 + 300}") \
ROUTE_ENV="KZ_ROUTE_LOOP=1000000 KZ_PERF_OUT=$out/frames.txt ${ROUTE_ENV:-}" \
  "$here/tools/route/route.sh" play "$route" "$start" > /dev/null &
runner=$!
for _ in $(seq 60); do [ -s "$pidfile" ] && break; sleep 1; done
pid=$(cat "$pidfile")
echo "t_s,rss_mb,threads,fds,vram_mb,loops" > "$out/samples.csv"
t0=$(date +%s); end=$((t0 + $(awk "BEGIN{printf \"%d\", $hours * 3600}")))
while kill -0 "$pid" 2>/dev/null && [ "$(date +%s)" -lt "$end" ]; do
    rss=$(awk '/^VmRSS/{print $2/1024}' "/proc/$pid/status" 2>/dev/null)
    thr=$(awk '/^Threads/{print $2}' "/proc/$pid/status" 2>/dev/null)
    fds=$(ls "/proc/$pid/fd" 2>/dev/null | wc -l)
    # the process table of nvidia-smi: "... <pid> <type> <name> <n>MiB |"
    vram=$(nvidia-smi 2>/dev/null | awk -v p="$pid" '$5 == p { v = $(NF-1); sub(/MiB/, "", v); print v }')
    loops=$(grep -ac '^\[route\] loop' "$out/game.log")
    echo "$(( $(date +%s) - t0 )),${rss:-0},${thr:-0},$fds,${vram:-0},$loops" >> "$out/samples.csv"
    sleep "$every"
done
kill -9 "$pid" 2>/dev/null; wait "$runner" 2>/dev/null
python3 "$here/tools/perf/frametimes.py" soak "$out"
