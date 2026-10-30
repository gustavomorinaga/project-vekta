#!/bin/sh
# Poor man's profiler for the port (no perf here): N stack samples of every
# thread of a running KZ1HD (started with KZ_PTRACER=1), plus a per-thread CPU
# snapshot. Prints the hottest frames per thread name.
# Usage: tools/stack_sample.sh [N] [OUT]
n=${1:-30}; out=${2:-/tmp/kz_stacks.txt}
pid=$(pgrep -x KZ1HD) || { echo "KZ1HD not running"; exit 1; }
top -H -b -n 1 -p "$pid" | sed -n '7,30p'
: > "$out"
i=0
while [ $i -lt "$n" ]; do
    eu-stack -p "$pid" -n 12 >> "$out" 2>/dev/null
    i=$((i + 1))
    sleep 0.3
done
echo "samples in $out"
# top frame of each thread sample, counted
awk '/^TID/ {f=1; next} f && /^#0/ {sub(/^#0 +0x[0-9a-f]+ +/, ""); print; f=0}' "$out" |
    sort | uniq -c | sort -rn | head -25
