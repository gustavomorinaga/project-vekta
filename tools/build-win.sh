#!/bin/sh
# Builds the Windows exe (work/build-win/KZ1HD.exe) on Linux, against the same lift as the Linux build
# (tools/lift.sh), with the pinned toolchain of tools/release/toolchain.json: llvm-mingw (clang, lld, libc++),
# SDL2, zlib, glslang, the Vulkan headers and FFmpeg's LGPL build. tools/release/fetch_toolchain.py downloads and
# checks them (work/toolchain-cache), tools/release/build_win_deps.py builds zlib and glslang with that compiler
# and stages everything in work/win-deps; both do nothing when that is already done. The exe links its C++ runtime
# statically; SDL2.dll and FFmpeg's DLLs are copied beside it (vulkan-1.dll comes with the GPU driver).
#
# KZ_WIN_TOOLCHAIN=system keeps the earlier route: the system clang and lld with the distribution's mingw-w64
# sysroot and its SDL2, zlib and glslang packages (Arch: mingw-w64-gcc mingw-w64-sdl2 mingw-w64-zlib
# mingw-w64-glslang), the host's Vulkan and FFmpeg headers, and the player's own FFmpeg DLLs.
set -e
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
HERE="$KZ_ROOT/port/kz1hd"; W="$KZ_WORK"; R="$PS3RECOMP_DIR"
B=${KZ_BUILD:-$KZ_WORK/build-win}
[ -f "$W/lift/gen/ppu_recomp.h" ] || { echo "no lift in $W/lift: run tools/lift.sh first" >&2; exit 1; }
LIFT="-DPS3RECOMP_DIR=$R -DRECOMP_DIR=$W/lift/gen -DSPU_DIR=$W/lift/spu -DLIBSRE_DIR=$W/fw/gen"

if [ "${KZ_WIN_TOOLCHAIN:-pinned}" = system ]; then
    H=$W/win-headers
    mkdir -p "$H"
    [ -d "$H/vulkan" ] || cp -r /usr/include/vulkan /usr/include/vk_video "$H/"
    [ -d "$H/libavcodec" ] || cp -r /usr/include/libavcodec /usr/include/libavformat /usr/include/libavutil \
                                    /usr/include/libswscale /usr/include/libswresample "$H/"
    # shellcheck disable=SC2086  # LIFT is a list of -D options
    cmake -G Ninja -S "$HERE" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$KZ_ROOT/port/cmake/clang-mingw.cmake" \
        -DCMAKE_BUILD_TYPE=Release $LIFT -DPS3RECOMP_RSX_VULKAN=ON -DPS3RECOMP_VULKAN_INCLUDE_DIR="$H" \
        -DKZ_FFMPEG_INCLUDE_DIR="$H" > /dev/null
    ninja -C "$B" ${KZ_JOBS:+-j "$KZ_JOBS"}
    M=/usr/x86_64-w64-mingw32/bin
    for d in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll libssp-0.dll SDL2.dll zlib1.dll glslang.dll \
             libglslang-default-resource-limits.dll; do
        cp -u "$M/$d" "$B/"
    done
    ls "$B"/avcodec-*.dll > /dev/null 2>&1 || echo "note: no FFmpeg DLLs in $B (the movies are skipped)"
    echo "built: $B/KZ1HD.exe"
    exit 0
fi

T="$W/toolchain-cache"; D="$W/win-deps"
"$KZ_PY" "$KZ_ROOT/tools/release/fetch_toolchain.py" --only llvm-mingw-linux,sdl2,zlib,glslang,vulkan-headers \
    > /dev/null
# FFmpeg: when its download is gone (its pin names a dated build), the host's headers and no DLLs.
"$KZ_PY" "$KZ_ROOT/tools/release/fetch_toolchain.py" --only ffmpeg > /dev/null ||
    echo "note: FFmpeg's pinned build could not be fetched: the host's headers, no movie DLLs"
"$KZ_PY" "$KZ_ROOT/tools/release/build_win_deps.py"
FF="$D/include"
if [ ! -f "$FF/libavcodec/avcodec.h" ]; then
    FF=$W/win-headers
    [ -d "$FF/libavcodec" ] || { mkdir -p "$FF" && cp -r /usr/include/libavcodec /usr/include/libavformat \
        /usr/include/libavutil /usr/include/libswscale /usr/include/libswresample "$FF/"; }
fi
# shellcheck disable=SC2086  # LIFT is a list of -D options
cmake -G Ninja -S "$HERE" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$KZ_ROOT/port/cmake/llvm-mingw.cmake" \
    -DLLVM_MINGW_ROOT="$T/llvm-mingw-linux" -DKZ_WIN_DEPS="$D" -DCMAKE_BUILD_TYPE=Release $LIFT \
    -DPS3RECOMP_RSX_VULKAN=ON -DPS3RECOMP_VULKAN_INCLUDE_DIR="$D/include" -DKZ_FFMPEG_INCLUDE_DIR="$FF" > /dev/null
ninja -C "$B" ${KZ_JOBS:+-j "$KZ_JOBS"}
# SDL2 and the FFmpeg libraries the movie loader opens (not avdevice or avfilter).
for d in "$D"/bin/*.dll; do
    case "${d##*/}" in avdevice-*|avfilter-*) ;; *) cp -u "$d" "$B/" ;; esac
done
ls "$B"/avcodec-*.dll > /dev/null 2>&1 || echo "note: no FFmpeg DLLs in $B (the movies are skipped)"
echo "built: $B/KZ1HD.exe"
