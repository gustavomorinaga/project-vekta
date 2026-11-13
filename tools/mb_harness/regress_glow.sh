#!/bin/sh
# Glow regression checks (no game run; needs a capture with the glow chain in MB_WORK, see README.md): builds the
# harness, the C reference CLI and the GPU tester, then the lifted SPU code = C stage by stage and over the whole
# chain (check_glow.py), and GPU = C on random and sparse sources at strengths 256, 128 and 0, and on a black source
# (the colour must come back unchanged). A frame whose alpha is not 255 (the MLAA leaves its flags there) with a black
# source at strengths 256 and 0: the add must still saturate the alpha, as the game's does. With GLOW_REPRO=<dump dir>
# ...: frames PS3RECOMP_MLAA_DUMP wrote while sprinting (seq_in, seq_glow, seq_field), as MLAA (C), glow (GPU) and
# motion blur (C), against MLAA, glow and motion blur all in C, every byte (the blur weighs its taps by alpha).
# Usage: tools/mb_harness/regress_glow.sh [capture dir]
set -u
H=$(cd "$(dirname "$0")" && pwd)
W=${MB_WORK:-$H/../../work/mb_cap}
R=${PS3RECOMP_DIR:-$H/../../work/ps3recomp-patched}
C=${1:-$W/cap2}
B=$W/hb; T=$W/t; mkdir -p "$B" "$T"
bash "$H/build.sh" "$B" || { echo "FAIL harness build"; exit 1; }
N=$H/../../port/kz1hd/native
cc -O2 -Wall -o "$B/glow_ref_cli" "$H/glow_ref_cli.c" "$N/glow_ref.c" || { echo "FAIL glow_ref build"; exit 1; }
cc -O2 -o "$B/glow_vk_test" "$H/glow_vk_test.c" -lvulkan || { echo "FAIL glow_vk_test build"; exit 1; }
glslangValidator -V --target-env vulkan1.1 "$R/libs/video/vulkan/rsx_vk_glow.comp" -o "$B/glow.spv" >/dev/null || { echo "FAIL shader"; exit 1; }
fail=0
MB_HB="$B" python3 "$H/check_glow.py" "$C" 1 || fail=1
for c in random:256 dots:256 dots:128 dots:0 black:256 alpha:256 alpha:0; do
    kind=${c%:*}; s=${c#*:}
    python3 -c "
import numpy as np
r = np.random.default_rng(9)
full = r.integers(0, 256, (720, 1280, 4), dtype=np.uint8)
if '$kind' != 'alpha': full[..., 0] = 255
src = np.zeros((360, 640, 4), np.uint8); src[..., 0] = 255
if '$kind' == 'random': src = r.integers(0, 256, (360, 640, 4), dtype=np.uint8)
if '$kind' == 'dots':
    for _ in range(80):
        y, x = r.integers(0, 360), r.integers(0, 640); src[max(0, y - 2):y + 3, max(0, x - 2):x + 3, 1:] = r.integers(64, 256, 3)
full.tofile('$T/gfull.argb'); src.tofile('$T/gsrc.argb')"
    "$B/glow_ref_cli" all "$T/gfull.argb" "$T/gsrc.argb" "$T/gref.argb" "$s"
    "$B/glow_vk_test" "$B/glow.spv" "$T/gfull.argb" "$T/gsrc.argb" "$T/ggpu.argb" "$s" 2>/dev/null
    if cmp -s "$T/gref.argb" "$T/ggpu.argb"; then echo "gpu = C ($kind, strength $s)"; else echo "FAIL gpu != C ($kind, strength $s)"; fail=1; fi
    if [ "$kind" != alpha ] && { [ "$kind" = black ] || [ "$s" = 0 ]; }; then
        cmp -s "$T/gfull.argb" "$T/ggpu.argb" && echo "  image unchanged" || { echo "FAIL the image changed"; fail=1; }
    fi
done
N2=$H/../../port/kz1hd/native
cc -O2 -o "$B/mlaa_ref_cli" "$H/../mlaa_harness/mlaa_ref_cli.c" "$N2/mlaa_ref.c" || { echo "FAIL mlaa_ref build"; exit 1; }
cc -O2 -o "$B/mb_ref_cli" "$H/mb_ref_cli.c" "$N2/mb_ref.c" "$N2/mlaa_ref.c" || { echo "FAIL mb_ref build"; exit 1; }
for d in ${GLOW_REPRO:-}; do
    for i in "$d"/*_in.argb; do
        s=${i%_in.argb}
        [ -f "${s}_glow.argb" ] && [ -f "${s}_field.argb" ] || continue
        "$B/mlaa_ref_cli" "$i" "$T/rm.argb"
        "$B/glow_vk_test" "$B/glow.spv" "$T/rm.argb" "${s}_glow.argb" "$T/rg.argb" 2>/dev/null
        "$B/mb_ref_cli" all "$T/rg.argb" "${s}_field.argb" "$T/rgpu.argb"
        "$B/glow_ref_cli" all "$T/rm.argb" "${s}_glow.argb" "$T/rc.argb"
        "$B/mb_ref_cli" all "$T/rc.argb" "${s}_field.argb" "$T/rref.argb"
        if cmp -s "$T/rgpu.argb" "$T/rref.argb"; then echo "repro $(basename "$d")/$(basename "$s"): gpu glow + C = C"
        else echo "FAIL repro $(basename "$d")/$(basename "$s"): gpu glow + C != C"; fail=1; fi
    done
done
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
