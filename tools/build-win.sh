#!/bin/sh
# Builds the Windows exe (work/build-win/KZ1HD.exe) on Linux: clang and
# lld with the mingw-w64 sysroot (port/cmake/clang-mingw.cmake), against the same
# lift as the Linux build (tools/lift.sh). Needs the MinGW packages of SDL2, zlib
# and glslang (Arch: mingw-w64-sdl2 mingw-w64-zlib mingw-w64-glslang); the
# Vulkan and FFmpeg headers are the host's, copied into $KZ_WORK/win-headers
# (headers only: vulkan-1.dll comes with the driver, and FFmpeg is loaded at
# run time). The DLLs the exe needs are copied beside it; FFmpeg's (9.x,
# avcodec-63 etc.) are the player's, e.g. a BtbN lgpl-shared build.
set -e
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
HERE="$KZ_ROOT/port/kz1hd"; W="$KZ_WORK"; R="$PS3RECOMP_DIR"
B=${KZ_BUILD:-$KZ_WORK/build-win}
H=$W/win-headers
mkdir -p "$H"
[ -d "$H/vulkan" ] || cp -r /usr/include/vulkan /usr/include/vk_video "$H/"
[ -d "$H/libavcodec" ] || cp -r /usr/include/libavcodec /usr/include/libavformat /usr/include/libavutil \
                                /usr/include/libswscale /usr/include/libswresample "$H/"
cmake -G Ninja -S "$HERE" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$KZ_ROOT/port/cmake/clang-mingw.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DPS3RECOMP_DIR="$R" -DRECOMP_DIR="$W/lift/gen" -DSPU_DIR="$W/lift/spu" -DLIBSRE_DIR="$W/fw/gen" \
    -DPS3RECOMP_RSX_VULKAN=ON -DPS3RECOMP_VULKAN_INCLUDE_DIR="$H" -DKZ_FFMPEG_INCLUDE_DIR="$H" > /dev/null
ninja -C "$B"
M=/usr/x86_64-w64-mingw32/bin
for d in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll libssp-0.dll SDL2.dll zlib1.dll glslang.dll \
         libglslang-default-resource-limits.dll; do
    cp -u "$M/$d" "$B/"
done
ls "$B"/avcodec-*.dll > /dev/null 2>&1 || echo "note: no FFmpeg DLLs in $B (the movies are skipped)"
echo "built: $B/KZ1HD.exe"
