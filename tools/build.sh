#!/bin/sh
# Linux build of the port: work/build/KZ1HD from the lift (tools/lift.sh) and the patched ps3recomp, with clang,
# Ninja and the Vulkan renderer. KZ_MARCH sets -march (default x86-64-v3; x86-64-v4 for AVX-512 machines).
# tools/pgo.sh rebuilds the same directory with a profile afterwards.
set -e
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
B=${KZ_BUILD:-$KZ_WORK/build}
[ -f "$KZ_WORK/lift/gen/ppu_recomp.h" ] || { echo "no lift in $KZ_WORK/lift: run tools/lift.sh first" >&2; exit 1; }
# -O1 for the base flags, as in the maintainer's build; the lifted PPU code gets KZ_PPU_OPT (-O2) on its own.
cmake -G Ninja -S "$KZ_ROOT/port/kz1hd" -B "$B" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS=-O1 -DCMAKE_CXX_FLAGS=-O1 -DKZ_MARCH="${KZ_MARCH:-x86-64-v3}" -DPS3RECOMP_RSX_VULKAN=ON \
    -DPS3RECOMP_DIR="$PS3RECOMP_DIR" -DRECOMP_DIR="$KZ_WORK/lift/gen" -DSPU_DIR="$KZ_WORK/lift/spu" \
    -DLIBSRE_DIR="$KZ_WORK/fw/gen" > /dev/null
ninja -C "$B" ${KZ_JOBS:+-j "$KZ_JOBS"}
echo "build: $B/KZ1HD"
