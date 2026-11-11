#!/bin/bash
# Alternating A/B benchmark of two builds on one recorded route: A B A B ... (or B A ... with AB_START=B), N pairs,
# each run under the shared benchmark lock (one game at a time across everything on the machine), then
# tools/perf/ab_report.py. A and B may be the same binary (an A/A run: the difference should be about 0).
#
#   tools/perf/ab.sh <A binary> <B binary> <route> <level n> <pairs> <out dir>
#
# Environment: everything tools/perf/route_play.sh reads (KZ_VFS, ROUTE_SAVE, ...), plus BENCH_LOCK (the lock file
# every game run on the machine shares; default $KZ_WORK/bench.lock, or work/bench.lock), A_ENV / B_ENV (extra game variables for one side only:
# an environment-switch A/B on one binary), AB_START=B (begin with B). Runs already finished in <out dir> are kept
# (the script can be restarted). Each run: <out dir>/<NN>-<A|B>.log/.env.
set -u
A=$(readlink -f "$1"); B=$(readlink -f "$2"); route=$3; n=$4; pairs=$5; out=$(readlink -m "$6")
here=$(dirname "$(readlink -f "$0")")
lock=${BENCH_LOCK:-${KZ_WORK:-$(cd "$here/../.." && pwd)/work}/bench.lock}
mkdir -p "$out"; touch "$lock"
{ echo "A $A $(sha256sum "$A" | cut -c1-16) ${A_ENV:-}"; echo "B $B $(sha256sum "$B" | cut -c1-16) ${B_ENV:-}"
  echo "route $route level $n pairs $pairs vfs ${KZ_VFS:-}"; } > "$out/setup.txt"
order="A B"; [ "${AB_START:-A}" = B ] && order="B A"
i=0
for p in $(seq "$pairs"); do
    for side in $order; do
        i=$((i + 1)); tag=$(printf '%02d-%s' "$i" "$side")
        grep -aq "^\[route\] replay ended" "$out/$tag.log" 2>/dev/null && continue
        if [ "$side" = A ]; then b=$A; e=${A_ENV:-}; else b=$B; e=${B_ENV:-}; fi
        echo "$(date +%T) pair $p $tag waiting for the lock" >> "$out/progress.txt"
        EXTRA_ENV="${EXTRA_ENV:-} $e" flock "$lock" "$here/route_play.sh" "$b" "$route" "$n" "$out/$tag" > /dev/null 2>&1
        echo "$(date +%T) pair $p $tag: $(grep -a '^\[route\] replay ended' "$out/$tag.log" | cut -c1-120)" >> "$out/progress.txt"
    done
done
python3 "$here/ab_report.py" "$out" | tee "$out/report.txt"
