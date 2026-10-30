#!/bin/sh
# Profile-guided build of the lifted PPU code (~15 minutes): an instrumented
# build plays into Level 1 for ten minutes, then work/build (tools/build.sh) is rebuilt with
# the merged profile. Run again after every PPU lift: a profile of other code is
# silently ignored. The profile stays in $KZ_WORK/pgo (it describes the game's code).
# KZ_BUILD: the build directory to profile and rebuild (default work/build).
set -e
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
HERE="$KZ_ROOT/port/kz1hd"; B="$KZ_WORK/build-pgo-gen"; P="$KZ_WORK/pgo"
BUILD=${KZ_BUILD:-$KZ_WORK/build}
cache() { grep "^$1:" "$BUILD/CMakeCache.txt" | cut -d= -f2-; }

cmake -G Ninja -S "$HERE" -B "$B" -DKZ_PGO_GEN=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS="$(cache CMAKE_C_FLAGS)" -DCMAKE_CXX_FLAGS="$(cache CMAKE_CXX_FLAGS)" \
    -DPS3RECOMP_DIR="$(cache PS3RECOMP_DIR)" -DKZ_MARCH="$(cache KZ_MARCH)" -DPS3RECOMP_RSX_VULKAN="$(cache PS3RECOMP_RSX_VULKAN)" \
    -DRECOMP_DIR="$(cache RECOMP_DIR)" -DSPU_DIR="$(cache SPU_DIR)" -DLIBSRE_DIR="$(cache LIBSRE_DIR)" > /dev/null
ninja -C "$B" > /dev/null

mkdir -p "$P"; rm -f "$P"/*.profraw
export KZ_VFS="${KZ_PGO_VFS:-$KZ_WORK/vfs-test}"   # not the player's saves: a copy of work/vfs (tools/setup-vfs.sh)
rm -rf "$KZ_VFS/gamedata/dev_hdd0/home/00000001/savedata/NPUA80856"
# The menus, the intro and Level 1 on a 3x guest clock; the instrumented
# build runs at a fraction of the speed, hence the long run.
KZ_BIN="$B/KZ1HD" KZ_PGO_DUMP=580 LLVM_PROFILE_FILE="$P/kz-%p.profraw" PS3_TIMEBASE_SCALE=3 KZ_VBLANK_HZ=240 \
PAD_SCRIPT="5:0x0008,9:0x4000,12:0x0008,15:0x4000,20:0x4000,22:0x4000,24:0x4000,26:0x4000,28:0x4000,30:0x4000,32:0x4000,34:0x4000,36:0x4000,38:0x4000,40:0x4000,60:0x4000,80:0x4000,100:0x4000,120:0x4000,140:0x4000,160:0x4000,180:0x4000" \
    "$KZ_ROOT/tools/run.sh" 595 "$KZ_WORK/run_pgo.log" > /dev/null
llvm-profdata merge -o "$P/kz.profdata" "$P"/*.profraw
rm -f "$P"/*.profraw

cmake -S "$HERE" -B "$BUILD" -DKZ_PGO_USE="$P/kz.profdata" > /dev/null
# the profile is not a dependency ninja knows: rebuild the lifted code with it
touch "$(cache RECOMP_DIR)"/*.cpp
ninja -C "$BUILD" > /dev/null
echo "build: $BUILD/KZ1HD with $P/kz.profdata"
