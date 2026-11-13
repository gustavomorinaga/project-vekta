#!/bin/sh
# check_dump.sh <dump dir> [strength/256]: the frames PS3RECOMP_MLAA_DUMP wrote in the game (seq_in, seq_glow,
# seq_field, seq_out .argb) through the C references in the renderer's order: MLAA (mlaa_ref), the glow
# (glow_ref, when seq_glow exists, at the given strength, default 256), the motion blur (mb_ref, when seq_field
# exists); the result must equal seq_out on every pixel. Prints one line per frame and PASS or FAIL.
set -u
H=$(cd "$(dirname "$0")" && pwd); N=$H/../../port/kz1hd/native
D=$1; S=${2:-256}
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
cc -O2 -o "$B/mlaa" "$H/../mlaa_harness/mlaa_ref_cli.c" "$N/mlaa_ref.c" || exit 1
cc -O2 -o "$B/glow" "$H/glow_ref_cli.c" "$N/glow_ref.c" || exit 1
cc -O2 -o "$B/mb" "$H/mb_ref_cli.c" "$N/mb_ref.c" "$N/mlaa_ref.c" || exit 1
fail=0; n=0
for i in "$D"/*_in.argb; do
    s=${i%_in.argb}; n=$((n + 1))
    "$B/mlaa" "$i" "$B/a.argb"
    [ -f "${s}_glow.argb" ] && { "$B/glow" all "$B/a.argb" "${s}_glow.argb" "$B/b.argb" "$S"; mv "$B/b.argb" "$B/a.argb"; }
    [ -f "${s}_field.argb" ] && { "$B/mb" all "$B/a.argb" "${s}_field.argb" "$B/b.argb"; mv "$B/b.argb" "$B/a.argb"; }
    r=$(python3 -c "
import numpy as np
a = np.fromfile('$B/a.argb', np.uint8).reshape(720, 1280, 4); o = np.fromfile('${s}_out.argb', np.uint8).reshape(720, 1280, 4)
i = np.fromfile('$i', np.uint8)[:720 * 1280 * 4].reshape(720, 1280, 4)
g = np.fromfile('${s}_glow.argb', np.uint8).reshape(360, 640, 4) if __import__('os').path.exists('${s}_glow.argb') else None
lit = int((g[..., 1:] > 0).any(-1).sum()) if g is not None else -1
print(int((a[..., 1:] != o[..., 1:]).any(-1).sum()), int((o[..., 1:] != i[..., 1:]).any(-1).sum()), lit)")
    set -- $r
    echo "$(basename "$s"): $1 pixels differ from C (colour); $2 changed by the passes; glow source lit $3"
    [ "$1" = 0 ] || fail=1
done
[ $n -gt 0 ] || { echo "FAIL: no frames in $D"; exit 1; }
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
