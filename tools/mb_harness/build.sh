#!/bin/bash
# build.sh [out dir, default $MB_WORK/hb]: the harness, linked with the lifted jm_post modules and policy module.
# PS3RECOMP_DIR / SPU_DIR: the toolkit checkout and the lifted SPU images (defaults: the v0.18 build's).
H=$(cd "$(dirname "$0")" && pwd); O=${1:-${MB_WORK:-$H/../../work/mb_cap}/hb}; mkdir -p $O
R=${PS3RECOMP_DIR:-$H/../../work/ps3recomp-patched}
D=${SPU_DIR:-$H/../../work/lift/spu}
F="-O2 -g -march=native -DSPU_FAST_FLOAT -DSPU_DEBUG_PROBES=0 -I$R/runtime/spu -I$D"
for m in jobmod_85fc8580d65e_e0030 jobmod_af58da8c8c70_e0030 jobmod_14e0d6b1737e_e0030 jobmod_de0b974339a8_e0030 jobmod_f60183d3d087_e0030 jobmod_6b716cb6ea57_e0030 jobmod_8197c3b77d98_e0030 jobmod_ab05f04899ea_e0030 pm_005B9180; do
    [ $O/$m.o -nt $D/$m/spu_recomp.c ] || clang $F -c $D/$m/spu_recomp.c -o $O/$m.o || exit 1
done
clang $F -Wall $H/harness.c $O/*.o -o $O/harness
