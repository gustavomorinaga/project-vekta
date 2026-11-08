#!/usr/bin/env bash
# The host tests that also build for Windows, cross-built with the pinned llvm-mingw (tools/release/toolchain.json)
# and run under Wine: the Windows side of the runtime's code (win32_compat, the _WIN32 branches) checked with no game,
# no GPU and no Windows machine. CI runs it in the windows-host job.
#   tools/wine-host-tests.sh
# Needs Wine (KZ_WINE, default the first of wine, wine64, /usr/lib/wine/wine64) and the network once, to fetch
# llvm-mingw and SDL2 into $KZ_WORK/toolchain-cache. Its own Wine prefix and the exes go in $KZ_WORK/wine-host
# (your ~/.wine is not touched). Exit 0: every test passed; 1: one failed; 2: setup.
set -uo pipefail
KZ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$KZ_ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/input/cellPad.c" ]] || { echo "wine-host-tests: no patched tree at $P; run tools/ps3recomp-patched.sh" >&2; exit 2; }
wine="${KZ_WINE:-}"
if [[ -z "$wine" ]]; then
  for w in wine wine64 /usr/lib/wine/wine64; do command -v "$w" >/dev/null && { wine="$w"; break; }; done
fi
[[ -n "$wine" ]] && command -v "$wine" >/dev/null || { echo "wine-host-tests: Wine not found (KZ_WINE)" >&2; exit 2; }
python3 "$KZ_ROOT/tools/release/fetch_toolchain.py" --only llvm-mingw-linux,sdl2 >/dev/null ||
  { echo "wine-host-tests: the pinned llvm-mingw and SDL2 could not be fetched" >&2; exit 2; }
T="$KZ_WORK/toolchain-cache"; B="$T/llvm-mingw-linux/bin"; SD="$T/sdl2/x86_64-w64-mingw32"
CC="$B/x86_64-w64-mingw32-clang"
[[ -x "$CC" && -f "$SD/bin/SDL2.dll" ]] || { echo "wine-host-tests: no llvm-mingw or SDL2 in $T" >&2; exit 2; }
O="$KZ_WORK/wine-host"; mkdir -p "$O/bin"   # each run writes every exe again
cp "$SD/bin/SDL2.dll" "$B/../x86_64-w64-mingw32/bin/libwinpthread-1.dll" "$O/bin/"
export WINEPREFIX="$O/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" DISPLAY=
# the flags the Windows build uses (port/cmake/llvm-mingw.cmake)
cflags=(-D_WIN32_WINNT=0x0A00 -O1 -UNDEBUG -w)
S="$P/runtime/spu"
spu=(); for u in spu_drain spu_lockstep spu_coherency spu_workload spurs_policy spurs_job spu_tsp_weak spu_vm_pagemap \
                spurs_policy_blob_weak spu_interp; do spu+=("$S/$u.c"); done

# name, then the compile arguments after the compiler and the common flags
build() { local name=$1; shift; "$CC" "${cflags[@]}" "$@" -o "$O/bin/$name.exe"; }
n=0; failed=()
run() { local name=$1; shift; n=$((n + 1))
  if ! build "$name" "$@" >"$O/$name.build.log" 2>&1; then
    echo "FAIL $name (build)"; tail -5 "$O/$name.build.log"; failed+=("$name"); return
  fi
  if (cd "$O/bin" && timeout 600 "$wine" "./$name.exe") >"$O/$name.log" 2>&1; then
    echo "ok   $name"
  else
    echo "FAIL $name"; tail -5 "$O/$name.log"; failed+=("$name")
  fi
}
run test_mlaa_ref -std=c11 -frounding-math "$KZ_ROOT/tools/tests/test_mlaa_ref.c" "$KZ_ROOT/port/kz1hd/native/mlaa_ref.c"
run test_spu_put_recheck -std=gnu17 -I "$P/include" -I "$S" -I "$P/runtime/platform" \
  "$KZ_ROOT/tools/tests/test_spu_put_recheck.c" "${spu[@]}" "$P/runtime/platform/win32_compat.c" -pthread
# SDL2's hints on Windows (patch 4012): the Raw Input joystick driver off unless the environment says otherwise
run test_pad_rawinput -std=gnu17 -DPS3RECOMP_PAD_USE_SDL2 -I "$P/include" -I "$P/libs/input" -I "$SD/include" -I "$SD/include/SDL2" \
  "$KZ_ROOT/tools/tests/test_pad_rawinput.c" -L "$SD/lib" -lSDL2 -pthread
# LV2 145's wall clock on Windows (patch 3032): not the time since boot
run test_wall_clock -std=gnu17 -I "$P/runtime/syscalls" "$KZ_ROOT/tools/tests/test_wall_clock.c"
# PS3_GCM_MAX_QUEUED_FLIPS's wait on Windows (patch 2055): a real wait, not 1000 rounds of Sleep(0), with 100 us naps
# on a high-resolution timer (patch 2058), not Sleep(1)
run test_gcm_flip_wait -std=gnu17 -I "$P/libs/video" "$KZ_ROOT/tools/tests/test_gcm_flip_wait.c"
echo "wine host tests: $((n - ${#failed[@]}))/$n pass${failed:+; failed: ${failed[*]}}"
(( ${#failed[@]} == 0 ))
