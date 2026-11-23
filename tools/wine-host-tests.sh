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
# 64-bit seeks in the EDAT reader on Windows (patch 4015): a long is 32 bits there
run test_edat_seek -std=gnu17 -I "$P/include" -I "$P/libs/filesystem/tests" "$KZ_ROOT/tools/tests/test_edat_seek.c"
# SDL2's hints on Windows (patch 4012): the Raw Input joystick driver off unless the environment says otherwise
run test_pad_rawinput -std=gnu17 -DPS3RECOMP_PAD_USE_SDL2 -I "$P/include" -I "$P/libs/input" -I "$SD/include" -I "$SD/include/SDL2" \
  "$KZ_ROOT/tools/tests/test_pad_rawinput.c" -L "$SD/lib" -lSDL2 -pthread
# LV2 145's wall clock on Windows (patch 3032): not the time since boot
run test_wall_clock -std=gnu17 -I "$P/runtime/syscalls" "$KZ_ROOT/tools/tests/test_wall_clock.c"
# PS3_GCM_MAX_QUEUED_FLIPS's wait on Windows (patch 2055): a real wait, not 1000 rounds of Sleep(0), with 100 us naps
# on a high-resolution timer (patch 2058), not Sleep(1)
run test_gcm_flip_wait -std=gnu17 -I "$P/libs/video" "$KZ_ROOT/tools/tests/test_gcm_flip_wait.c"
# Windows display scaling (port/kz1hd/win_dpi.h): per-monitor DPI aware once SDL's video starts
run test_win_dpi -std=gnu17 -I "$KZ_ROOT/port/kz1hd" -I "$SD/include" -I "$SD/include/SDL2" \
  "$KZ_ROOT/tools/tests/test_win_dpi.c" -L "$SD/lib" -lSDL2
# KZ1HD.exe's resources (port/kz1hd/app.rc): the version block and the manifest, compiled into an exe in a folder
# with a space in its path, read back with GetFileVersionInfoW and FindResource
n=$((n + 1)); R="$O/exe resources"; mkdir -p "$R"
if cmake -DKZ_VERSION_FILE="$KZ_ROOT/VERSION" -DKZ_PATCHSET_FILE="$P/.patchset" -DKZ_VERSION_OUT="$R/kz_version.h" \
     -P "$KZ_ROOT/port/kz1hd/version.cmake" >"$O/test_exe_resources.build.log" 2>&1 &&
   "$B/x86_64-w64-mingw32-windres" -I "$R" -O coff -o "$R/app.res.o" "$KZ_ROOT/port/kz1hd/app.rc" \
     >>"$O/test_exe_resources.build.log" 2>&1 &&
   "$CC" "${cflags[@]}" -I "$R" "$KZ_ROOT/tools/tests/test_exe_resources.c" "$R/app.res.o" -lversion \
     -o "$R/test_exe_resources.exe" >>"$O/test_exe_resources.build.log" 2>&1; then
  if (cd "$R" && timeout 600 "$wine" "./test_exe_resources.exe") >"$O/test_exe_resources.log" 2>&1; then
    echo "ok   test_exe_resources"
  else
    echo "FAIL test_exe_resources"; tail -5 "$O/test_exe_resources.log"; failed+=(test_exe_resources)
  fi
else
  echo "FAIL test_exe_resources (build)"; tail -5 "$O/test_exe_resources.build.log"; failed+=(test_exe_resources)
fi
# The CPU check (port/kz1hd/cpu_level.h, cpu_check.c): the levels of real CPUs' bits, then cpu_check.c (baseline
# x86-64) beside a C++ static initializer (x86-64-v3) in a folder with a space: with KZ_CPU_LEVEL_MAX=2 the exe exits
# with KZ_EXIT_CPU before the initializer, its line in KZ_LOG; otherwise it runs
run test_cpu_level -std=c11 -I "$KZ_ROOT/port/kz1hd" "$KZ_ROOT/tools/tests/test_cpu_level.c"
n=$((n + 1)); C="$O/cpu check"; mkdir -p "$C"; want=$(sed -n 's/^#define KZ_EXIT_CPU *\([0-9]*\).*/\1/p' "$KZ_ROOT/port/kz1hd/kz_exit.h")
if "$CC" -O2 -march=x86-64 -DKZ_CPU_REQUIRED=3 -I "$KZ_ROOT/port/kz1hd" -c "$KZ_ROOT/port/kz1hd/cpu_check.c" -o "$C/cpu_check.o" \
     >"$O/test_cpu_check.build.log" 2>&1 &&
   "$B/x86_64-w64-mingw32-clang++" -O2 -march=x86-64-v3 "$C/cpu_check.o" "$KZ_ROOT/tools/tests/test_cpu_check_init.cpp" \
     -static -o "$C/test_cpu_check.exe" >>"$O/test_cpu_check.build.log" 2>&1; then
  ok=1
  (cd "$C" && timeout 120 "$wine" ./test_cpu_check.exe) >"$O/test_cpu_check.log" 2>&1 || ok=0
  grep -q "static initializer" "$O/test_cpu_check.log" || ok=0
  (cd "$C" && KZ_CPU_LEVEL_MAX=2 KZ_LOG="$C/k z.log" timeout 120 "$wine" ./test_cpu_check.exe) >"$O/test_cpu_check.low.log" 2>&1
  rc=$?
  [[ $rc == "$want" ]] || ok=0
  grep -q "static initializer" "$O/test_cpu_check.low.log" && ok=0
  grep -q "^\[kz\] CPU check: .*x86-64-v2" "$C/k z.log" || ok=0
  if (( ok )); then echo "ok   test_cpu_check"; else
    echo "FAIL test_cpu_check (low level: exit $rc, want $want)"; tail -3 "$O/test_cpu_check.log" "$O/test_cpu_check.low.log"
    failed+=(test_cpu_check)
  fi
else
  echo "FAIL test_cpu_check (build)"; tail -5 "$O/test_cpu_check.build.log"; failed+=(test_cpu_check)
fi
# The EBOOT check (port/kz1hd/eboot_check.h): SHA-256 vectors, equal / changed / truncated / missing, in a folder whose
# name has a space and accents (ANSI argv and fopen, as ppu_load_elf opens the EBOOT)
acc=$'A\xc3\xa7\xc3\xb5es e EBOOT'   # "Acoes" with c-cedilla and o-tilde: Wine hands it to the exe in the ANSI code page
n=$((n + 1)); rm -rf "${O:?}/$acc"; mkdir -p "$O/$acc"
if build test_eboot_check -std=gnu17 -I "$KZ_ROOT/port/kz1hd" "$KZ_ROOT/tools/tests/test_eboot_check.c" \
     >"$O/test_eboot_check.build.log" 2>&1; then
  if (cd "$O" && timeout 600 "$wine" "bin/test_eboot_check.exe" "$acc") >"$O/test_eboot_check.log" 2>&1; then
    echo "ok   test_eboot_check"
  else
    echo "FAIL test_eboot_check"; grep FAIL "$O/test_eboot_check.log" | head -5; failed+=(test_eboot_check)
  fi
else
  echo "FAIL test_eboot_check (build)"; tail -5 "$O/test_eboot_check.build.log"; failed+=(test_eboot_check)
fi
# The crash reports and the hang watchdog (port/kz1hd/crash_report.c, sys_info.c, with ps3recomp's ps3_stdio.c): each
# KZ_CRASH_TEST kind in a child process, its [kz-crash] block, exit code, signature and copy in KZ_CRASH_DIR; the
# [kz-hang] reports from a Toolhelp snapshot with each thread stopped, copied and unwound; the [kz-sys] lines
n=$((n + 1)); rm -rf "${O:?}/crash-report"
if build test_crash_report -std=gnu17 -O2 -I "$KZ_ROOT/port/kz1hd" -I "$P/include" -I "$P/runtime/platform" \
     "$KZ_ROOT/tools/tests/test_crash_report.c" "$KZ_ROOT/port/kz1hd/crash_report.c" "$KZ_ROOT/port/kz1hd/sys_info.c" \
     "$P/runtime/platform/ps3_stdio.c" >"$O/test_crash_report.build.log" 2>&1; then
  if (cd "$O/bin" && timeout 900 "$wine" ./test_crash_report.exe ../crash-report) >"$O/test_crash_report.log" 2>&1; then
    echo "ok   test_crash_report"
  else
    echo "FAIL test_crash_report"; grep FAIL "$O/test_crash_report.log" | head -10; failed+=(test_crash_report)
  fi
else
  echo "FAIL test_crash_report (build)"; tail -5 "$O/test_crash_report.build.log"; failed+=(test_crash_report)
fi
echo "wine host tests: $((n - ${#failed[@]}))/$n pass${failed:+; failed: ${failed[*]}}"
(( ${#failed[@]} == 0 ))
