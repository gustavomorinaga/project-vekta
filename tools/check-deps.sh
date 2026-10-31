#!/usr/bin/env bash
# Checks that the tools and libraries docs/BUILDING.md lists are there, before a lift or a build fails half-way.
# Prints one line per item and exits 1 when something required is missing. Reads nothing of the game.
#   tools/check-deps.sh [--win]   --win also checks the Windows cross build (tools/build-win.sh)
set -uo pipefail
KZ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$KZ_ROOT/tools/env.sh"
WIN=0; [[ "${1:-}" == --win ]] && WIN=1
missing=0
row() { printf '%-9s %-34s %s\n' "$1" "$2" "$3"; }
need() {   # need <name> <hint> <test...>: required
  local name=$1 hint=$2; shift 2
  if "$@" >/dev/null 2>&1; then row ok "$name" ""; else row MISSING "$name" "$hint"; missing=1; fi
}
want() {   # want <name> <hint> <test...>: optional
  local name=$1 hint=$2; shift 2
  if "$@" >/dev/null 2>&1; then row ok "$name" ""; else row optional "$name" "$hint"; fi
}
pc() { pkg-config --exists "$1"; }
py_ok() { "$1" -c 'import sys; sys.exit(sys.version_info < (3, 10))'; }

need "clang, clang++"                "clang (Arch: clang)"                    command -v clang clang++
need "lld"                           "lld (Arch: lld)"                        command -v ld.lld
need "Python 3.10+"                  "python3 (Arch: python)"                 py_ok python3
need "venv (work/venv)"              "python3 -m venv work/venv (BUILDING.md, step 2)"  test -x "$KZ_WORK/venv/bin/python"
need "CMake"                         "work/venv/bin/pip install cmake"        command -v cmake
need "Ninja"                         "work/venv/bin/pip install ninja"        command -v ninja
need "pkg-config"                    "pkgconf (Arch: pkgconf)"                command -v pkg-config
need "SDL2"                          "sdl2-compat or SDL2 development files"  pc sdl2
need "zlib"                          "zlib development files"                 pc zlib
need "Vulkan headers"                "vulkan-headers"                         test -f /usr/include/vulkan/vulkan.h
need "Vulkan loader"                 "vulkan-icd-loader"                      pc vulkan
need "glslang"                       "glslang (glslangValidator)"             command -v glslangValidator
want "FFmpeg (movies)"               "ffmpeg; without it the movies are skipped"  pc libavcodec
want "llvm-profdata (tools/pgo.sh)"  "llvm"                                   command -v llvm-profdata
want "bwrap (tools/sandbox.sh)"      "bubblewrap"                             command -v bwrap
if (( WIN )); then
  # The pinned toolchain and libraries come from tools/release/toolchain.json (fetched on the first build);
  # KZ_WIN_TOOLCHAIN=system uses the distribution's mingw-w64 packages instead.
  if [[ "${KZ_WIN_TOOLCHAIN:-pinned}" == system ]]; then
    need "MinGW-w64 sysroot"         "mingw-w64-gcc"                          test -d /usr/x86_64-w64-mingw32/include
    need "SDL2 for MinGW"            "mingw-w64-sdl2 (AUR)"                   test -f /usr/x86_64-w64-mingw32/lib/pkgconfig/sdl2.pc
    need "zlib for MinGW"            "mingw-w64-zlib (AUR)"                   test -f /usr/x86_64-w64-mingw32/lib/pkgconfig/zlib.pc
  else
    want "pinned Windows toolchain"  "fetched by tools/build-win.sh on its first run (network)" \
      test -d "${KZ_WORK:-work}/toolchain-cache/llvm-mingw-linux/bin"
  fi
  want "Wine (tools/wine-test.sh)"   "wine; or --proton with a Proton folder" command -v "${KZ_WINE:-wine}"
fi
(( missing )) && echo "check-deps: something required is missing (docs/BUILDING.md, step 2)" || echo "check-deps: ok"
exit "$missing"
