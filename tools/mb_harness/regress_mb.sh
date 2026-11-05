#!/bin/sh
# Motion blur regression checks (no game run; needs a capture in MB_WORK, see README.md): builds the harness,
# the C reference CLI and the GPU tester, then the lifted SPU code = C stage by stage (check_mb.py) and
# GPU = C on random images and fields, and on a field with no strength (the image must come back unchanged).
# Usage: tools/mb_harness/regress_mb.sh [capture dir]
set -u
H=$(cd "$(dirname "$0")" && pwd)
W=${MB_WORK:-$H/../../work/mb_cap}
R=${PS3RECOMP_DIR:-$H/../../work/ps3recomp-patched}
C=${1:-$W/cap2}
B=$W/hb; T=$W/t; mkdir -p "$B" "$T"
"$H/build.sh" "$B" || { echo "FAIL harness build"; exit 1; }
cc -O2 -o "$B/mb_ref_cli" "$H/mb_ref_cli.c" "$H/../../port/kz1hd/native/mb_ref.c" "$H/../../port/kz1hd/native/mlaa_ref.c" || { echo "FAIL mb_ref build"; exit 1; }
cc -O2 -o "$B/mb_vk_test" "$H/mb_vk_test.c" -lvulkan || { echo "FAIL mb_vk_test build"; exit 1; }
glslangValidator -V --target-env vulkan1.1 "$R/libs/video/vulkan/rsx_vk_mb.comp" -o "$B/mb.spv" >/dev/null || { echo "FAIL shader"; exit 1; }
fail=0
out=$(MB_HB="$B" python3 "$H/check_mb.py" "$C" 1) || fail=1
echo "$out"; echo "$out" | grep -q " [1-9][0-9]* of " && fail=1
for seed in 1 2 zero; do
    python3 -c "
import numpy as np, sys
r = np.random.default_rng(7 if '$seed' == 'zero' else int('$seed'))
r.integers(0, 256, (720, 1280, 4), dtype=np.uint8).tofile('$T/full.argb')
f = r.integers(0, 256, (360, 640, 4), dtype=np.uint8)
if '$seed' == 'zero': f[..., 0] = 0
f.tofile('$T/fld.argb')"
    "$B/mb_ref_cli" all "$T/full.argb" "$T/fld.argb" "$T/ref.argb"
    "$B/mb_vk_test" "$B/mb.spv" "$T/full.argb" "$T/fld.argb" "$T/gpu.argb" 2>/dev/null
    if cmp -s "$T/ref.argb" "$T/gpu.argb"; then echo "gpu = C ($seed)"; else echo "FAIL gpu != C ($seed)"; fail=1; fi
    if [ "$seed" = zero ] && ! cmp -s "$T/full.argb" "$T/gpu.argb"; then echo "FAIL no strength changed the image"; fail=1; fi
done
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
