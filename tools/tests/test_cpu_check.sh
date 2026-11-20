#!/usr/bin/env bash
# Host test for the CPU check (port/kz1hd/cpu_level.h, cpu_check.c, kz_exit.h; CMakeLists.txt's rule for them):
#  - test_cpu_level.c: the levels of real CPUs' feature bits, and this machine's level equals the one
#    tools/check-game-files.py reads from /proc/cpuinfo (the first run's CPU line);
#  - a two-file program built by CMake, in a folder with a space in its path, with port/kz1hd/CMakeLists.txt's own
#    lines for cpu_check.c: cpu_check.c and a C++ static initializer compiled for x86-64-v3. The check's object has no
#    VEX-encoded or other x86-64-v3 instruction; with the CPU's level (KZ_CPU_LEVEL_MAX=2) below the build's it exits
#    with KZ_EXIT_CPU before the static initializer, with its line on stderr and in KZ_LOG; otherwise it runs;
#  - with qemu-x86_64 (qemu-user) and the pinned Zig: the same two files, linked static against baseline musl, on an
#    emulated Nehalem (x86-64-v2) exit with KZ_EXIT_CPU, never 132 (SIGILL), and run on an emulated Haswell. CI's
#    checks job has both and sets KZ_REQUIRE_QEMU=1, which makes their absence a failure; elsewhere it is noted;
#  - launch.py's EXIT_CPU is kz_exit.h's KZ_EXIT_CPU; tools/kzbuild.py's level follows KZ_MARCH as CMake's does.
# The Windows build of the same program runs under Wine in tools/wine-host-tests.sh. No game data, no GPU.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
K="$ROOT/port/kz1hd"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
d="$out/cpu check"; mkdir -p "$d"
fails=0
check() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi; }
is() { local what=$1; shift; "$@"; check $? "$what"; }   # is WHAT TEST...: the test's result as a check
CC=${CC:-clang}; CXX=${CXX:-clang++}
command -v cmake >/dev/null && command -v "$CC" >/dev/null && command -v "$CXX" >/dev/null ||
  { echo "FAIL: cmake, $CC and $CXX are needed" >&2; exit 1; }
want=$(sed -n 's/^#define KZ_EXIT_CPU *\([0-9]*\).*/\1/p' "$K/kz_exit.h")

"$CC" -std=c11 -O1 -Wall -Wextra -Werror -I "$K" "$ROOT/tools/tests/test_cpu_level.c" -o "$d/test_cpu_level"
"$d/test_cpu_level" >"$d/level.log"; check $? "test_cpu_level: $(tail -1 "$d/level.log")"
grep '^FAIL' "$d/level.log"
here=$(sed -n 's/^this CPU: x86-64-v\([0-9]\)$/\1/p' "$d/level.log")
proc=$(python3 -c "import importlib.util as u, sys; s = u.spec_from_file_location('c', sys.argv[1]); m = u.module_from_spec(s)
s.loader.exec_module(m); print(m.cpu_level(None, windows=False))" "$ROOT/tools/check-game-files.py")
is "CPUID says x86-64-v$here, /proc/cpuinfo's flags say x86-64-v$proc" [ -n "$here" ] && [ "$here" = "$proc" ]

# CMakeLists.txt's own lines for cpu_check.c, between "set(KZ_CPU_REQUIRED 0)" and its status message
rule=$(sed -n '/^set(KZ_CPU_REQUIRED 0)$/,/^message(STATUS "CPU check:/p' "$K/CMakeLists.txt")
is "CMakeLists.txt has the cpu_check.c rule" [ -n "$rule" ]
cp "$K/cpu_check.c" "$K/cpu_level.h" "$K/kz_exit.h" "$ROOT/tools/tests/test_cpu_check_init.cpp" "$d/"
cat >"$d/CMakeLists.txt" <<CM
cmake_minimum_required(VERSION 3.20)
project(cpucheck LANGUAGES C CXX)
set(KZ_MARCH "x86-64-v3")
add_compile_options(-march=\${KZ_MARCH} -O2)
$rule
add_executable(t cpu_check.c test_cpu_check_init.cpp)
CM
cmake -S "$d" -B "$d/build" -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" >"$d/cmake.log" 2>&1 &&
  cmake --build "$d/build" >>"$d/cmake.log" 2>&1
check $? "the two-file program builds with CMake in a folder with a space in its path"
t="$d/build/t"
obj=$(find "$d/build" -name 'cpu_check.c.o' | head -1)
grep -q 'CPU check:       x86-64-v3' "$d/cmake.log"; check $? "KZ_MARCH=x86-64-v3 builds the check for x86-64-v3"
# VEX (AVX, BMI1/2, FMA, F16C: every such mnemonic starts with v or is a BMI one) and the other v3 instructions
V3_INSN='^\s+[0-9a-f]+:\s+(v[a-z0-9]+|andn|bextr|blsi|blsmsk|blsr|bzhi|mulx|pdep|pext|rorx|sarx|shlx|shrx|lzcnt|tzcnt|movbe)\b'
printf 'unsigned f(unsigned a, unsigned b) { return a >> b; }\n' >"$d/v3.c" && "$CC" -O2 -march=x86-64-v3 -c "$d/v3.c" -o "$d/v3.o"
objdump -d --no-show-raw-insn "$d/v3.o" | grep -qE "$V3_INSN"; check $? "the instruction scan sees a v3 object's shrx (the scan works)"
v3=$(objdump -d --no-show-raw-insn "$obj" | grep -E "$V3_INSN" | head -3)
is "the check's object has no VEX or other x86-64-v3 instruction ${v3:+($v3)}" [ -n "$obj" ] && [ -z "$v3" ]

if [ "$here" -ge 3 ]; then
  o=$("$t" 2>&1); rc=$?
  is "on this x86-64-v$here CPU it runs (exit $rc)" [ $rc = 0 ] && [ "$o" = $'static initializer\nmain' ]
  o=$(KZ_CPU_LEVEL_MAX=3 "$t" 2>&1); rc=$?
  is "KZ_CPU_LEVEL_MAX=3, the build's level: it runs" [ $rc = 0 ]
else
  echo "FAIL this machine is x86-64-v$here: the program built for x86-64-v3 cannot be checked running here"; fails=$((fails + 1))
fi
o=$(KZ_CPU_LEVEL_MAX=2 KZ_LOG="$d/k z.log" "$t" 2>&1); rc=$?
is "KZ_CPU_LEVEL_MAX=2: exit $rc, KZ_EXIT_CPU is $want" [ "$rc" = "$want" ]
case $o in "[kz] CPU check: This build of Project Vekta needs a CPU with x86-64-v3 "*", and this one has x86-64-v2. "*) r=0;; *) r=1;; esac
check $r "its line on stderr names both levels"
case $o in *"static initializer"*|*main*) r=1;; *) r=0;; esac
check $r "nothing of the program ran before it (no static initializer, no main)"
grep -q '^\[kz\] CPU check: .*x86-64-v2' "$d/k z.log"; check $? "the same line is written into KZ_LOG"

# The emulated Nehalem (x86-64-v2). A dynamically linked program cannot be used for it: on Ubuntu's runner and on
# CachyOS alike, even a trivial one exits with SIGILL there before main (the system's loader and libc). So the same
# two files are linked fully static against musl built for baseline x86-64 by the pinned Zig (KZ_ZIG, else
# $KZ_WORK/toolchain-cache/zig/zig; tools/release/fetch_toolchain.py --only zig): cpu_check.c and the C++ static
# initializer as CMakeLists.txt compiles them (baseline, x86-64-v3), the libc and the link at baseline. A trivial
# program built that way must run on the Nehalem (the emulation works), and the two-file one must exit with
# KZ_EXIT_CPU, never 132. CI's checks job fetches Zig, installs qemu-user and sets KZ_REQUIRE_QEMU=1: there a missing
# qemu or Zig is a failure; elsewhere it is noted and the run is CI's.
zig=${KZ_ZIG:-${KZ_WORK:-$ROOT/work}/toolchain-cache/zig/zig}
if command -v qemu-x86_64 >/dev/null && [ -x "$zig" ]; then
  z="$d/zig musl"; mkdir -p "$z"
  ZIG_GLOBAL_CACHE_DIR="$z/cache" ZIG_LOCAL_CACHE_DIR="$z/cache" sh -c '
    zig=$1 d=$2 z=$3
    printf "int main(void) { return 0; }\n" >"$z/triv.c" &&
    "$zig" cc -target x86_64-linux-musl -mcpu=x86_64 -static -O1 "$z/triv.c" -o "$z/triv" &&
    "$zig" cc -target x86_64-linux-musl -mcpu=x86_64 -O2 -DKZ_CPU_REQUIRED=3 -I "$d" -c "$d/cpu_check.c" -o "$z/cpu_check.o" &&
    "$zig" c++ -target x86_64-linux-musl -mcpu=x86_64_v3 -O2 -c "$d/test_cpu_check_init.cpp" -o "$z/init.o" &&
    "$zig" c++ -target x86_64-linux-musl -mcpu=x86_64 -static "$z/cpu_check.o" "$z/init.o" -o "$z/t"' _ "$zig" "$d" "$z" \
    >"$z/build.log" 2>&1
  check $? "the static two-file program builds with the pinned Zig against baseline musl"
  (qemu-x86_64 -cpu Nehalem "$z/triv") >/dev/null 2>&1
  check $? "a trivial static baseline program runs on the emulated Nehalem (the emulation itself works)"
  (qemu-x86_64 -cpu Nehalem "$z/t") >"$d/qemu.log" 2>&1; rc=$?
  is "under qemu-x86_64 -cpu Nehalem: exit $rc (KZ_EXIT_CPU $want; 132 would be SIGILL)" [ "$rc" = "$want" ]
  grep -q 'this one has x86-64-v2' "$d/qemu.log"; check $? "and it names the emulated CPU as x86-64-v2"
  r=0; grep -q 'static initializer' "$d/qemu.log" && r=1; check $r "and nothing of the program ran before it"
  (qemu-x86_64 -cpu Haswell "$z/t") >"$d/qemu-hsw.log" 2>&1; rc=$?
  is "under -cpu Haswell (x86-64-v3) the same program runs: exit $rc" [ "$rc" = 0 ]
elif [ "${KZ_REQUIRE_QEMU:-0}" = 1 ]; then
  echo "FAIL qemu-x86_64 or the pinned Zig ($zig) not found (KZ_REQUIRE_QEMU=1)"; fails=$((fails + 1))
else
  echo "note qemu-x86_64 or the pinned Zig ($zig) is not here: the run on an emulated Nehalem is CI's (KZ_REQUIRE_QEMU=1)"
fi

is "launch.py's EXIT_CPU is kz_exit.h's KZ_EXIT_CPU ($want)" \
  [ "$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import launch; print(launch.EXIT_CPU)' "$ROOT/tools/release")" = "$want" ]
python3 - "$ROOT" <<'PY'; check $? "tools/kzbuild.py's level follows KZ_MARCH as CMakeLists.txt's rule does"
import sys
sys.path.insert(0, sys.argv[1] + "/tools")
import kzbuild
assert kzbuild.march({}) == "x86-64-v3" and kzbuild.march({"KZ_MARCH": "x86-64-v4"}) == "x86-64-v4"
assert [kzbuild.cpu_required(m) for m in ("x86-64-v2", "x86-64-v3", "x86-64-v4", "native", "", "x86-64")] == [2, 3, 4, 0, 0, 0]
src = open(sys.argv[1] + "/tools/kzbuild.py").read()
assert src.count('f"-DKZ_MARCH={march(e)}"') == 3, "the Linux, pinned Linux and Windows builds all pass KZ_MARCH"
PY
echo "test_cpu_check: $([ $fails = 0 ] && echo ok || echo "FAIL ($fails)")"
[ $fails = 0 ]
