#!/bin/sh
# Profile-guided build of the lifted PPU code: an instrumented build plays, then the build directory is rebuilt with
# the merged profile. Run again after every PPU lift: a profile of other code does not apply (the check at the end
# says so). The profile stays in $KZ_WORK/pgo (it describes the game's code).
#
# The play is one of:
#   - by default (~15 minutes): the menus, the intro and Level 1 on a 3x guest clock for ten minutes (PAD_SCRIPT)
#   - KZ_PGO_ROUTES="<route> ...": recorded routes (port/kz1hd/route.cpp) replayed one after the other, loose and
#     headless, on the clock they were recorded at; each starts from a new profile, Campaign > Select level 1 >
#     Normal, so record them from there (KZ_ROUTE_REC). Combat routes made a faster build than the default play
#     (docs/BUILDING.md). Each replay quits at its end and writes its profile then; KZ_PGO_ROUTE_MAX (default
#     900 seconds) stops a replay that does not end.
#
# KZ_BUILD: the build directory to profile and rebuild (default work/build); it has to be one tools/build.sh
#   configured (CMake keeps the directory's own path in it: a copy of one cannot be reconfigured).
# KZ_PGO_VFS: the folder the instrumented build plays from (default work/vfs-test). Made from work/vfs
#   (tools/setup-vfs.sh) when missing, with its own copy of every file the game writes, so your saves are safe.
set -e
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
HERE="$KZ_ROOT/port/kz1hd"; B="$KZ_WORK/build-pgo-gen"; P="$KZ_WORK/pgo"
BUILD=${KZ_BUILD:-$KZ_WORK/build}
[ -f "$BUILD/CMakeCache.txt" ] || { echo "pgo.sh: no configured build in $BUILD: run tools/build.sh first" >&2; exit 1; }
cache() { grep "^$1:" "$BUILD/CMakeCache.txt" | cut -d= -f2-; }
mkdir -p "$P"
# quiet unless it fails: then the end of its log
quiet() { log=$1; shift; "$@" > "$log" 2>&1 || { tail -n 40 "$log" >&2; echo "pgo.sh: failed: $* (log: $log)" >&2; exit 1; }; }

# The play's folder: a copy of work/vfs with the movies linked (never written) and every other file copied.
T="${KZ_PGO_VFS:-$KZ_WORK/vfs-test}"
if [ ! -d "$T/PS3_GAME" ]; then
    [ -d "$KZ_VFS/PS3_GAME" ] || { echo "pgo.sh: no $KZ_VFS/PS3_GAME: run tools/setup-vfs.sh first" >&2; exit 1; }
    rm -rf "$T"; cp -al "$KZ_VFS" "$T"
    find "$T" -type f ! -path '*/USRDIR/Movies/*' ! -path '*/USRDIR/Menu/*' \
        -exec sh -c 'for f do cp -p "$f" "$f.kzcp" && mv -f "$f.kzcp" "$f"; done' sh {} +
    echo "pgo.sh: made $T from $KZ_VFS"
fi
export KZ_VFS="$T"

quiet "$P/configure-gen.log" cmake -G Ninja -S "$HERE" -B "$B" -DKZ_PGO_GEN=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS="$(cache CMAKE_C_FLAGS)" -DCMAKE_CXX_FLAGS="$(cache CMAKE_CXX_FLAGS)" \
    -DPS3RECOMP_DIR="$(cache PS3RECOMP_DIR)" -DKZ_MARCH="$(cache KZ_MARCH)" -DPS3RECOMP_RSX_VULKAN="$(cache PS3RECOMP_RSX_VULKAN)" \
    -DRECOMP_DIR="$(cache RECOMP_DIR)" -DSPU_DIR="$(cache SPU_DIR)" -DLIBSRE_DIR="$(cache LIBSRE_DIR)"
quiet "$P/build-gen.log" ninja -C "$B"

rm -f "$P"/*.profraw
SAVES="$KZ_VFS/gamedata/dev_hdd0/home/00000001/savedata"
if [ -z "${KZ_PGO_ROUTES:-}" ]; then
    rm -rf "$SAVES/NPUA80856"
    # The menus, the intro and Level 1 on a 3x guest clock; the instrumented
    # build runs at a fraction of the speed, hence the long run.
    KZ_BIN="$B/KZ1HD" KZ_PGO_DUMP=580 LLVM_PROFILE_FILE="$P/kz-%p.profraw" PS3_TIMEBASE_SCALE=3 KZ_VBLANK_HZ=240 \
    PAD_SCRIPT="5:0x0008,9:0x4000,12:0x0008,15:0x4000,20:0x4000,22:0x4000,24:0x4000,26:0x4000,28:0x4000,30:0x4000,32:0x4000,34:0x4000,36:0x4000,38:0x4000,40:0x4000,60:0x4000,80:0x4000,100:0x4000,120:0x4000,140:0x4000,160:0x4000,180:0x4000" \
        "$KZ_ROOT/tools/run.sh" 595 "$KZ_WORK/run_pgo.log" > /dev/null
else
    pad="$P/pad.txt"; n=0
    for route in $KZ_PGO_ROUTES; do
        route=$(readlink -f "$route"); n=$((n + 1)); log="$KZ_WORK/run_pgo_route$n.log"
        [ -f "$route" ] || { echo "pgo.sh: no route $route" >&2; exit 1; }
        hz=$(sed -n 's/^H tick_hz \([0-9]*\).*/\1/p' "$route" | head -n 1); hz=${hz:-30}
        # a new profile on the route's clock (the PC menu's frame-rate limit sets the game clock)
        rm -rf "$SAVES/NPUA80856"; mkdir -p "$SAVES"
        printf 'fps_limit=%s\nintro=off\n' "$hz" > "$KZ_VFS/kz1hd_pc.ini"
        : > "$pad"
        echo "pgo.sh: route $n: $route ($hz Hz)"
        (cd "$KZ_VFS" && KZ_UNLOCK_LEVELS=1 KZ_ROUTE_PLAY="$route" KZ_ROUTE_EXIT=1 KZ_ROUTE_LOOSE=1 KZ_TICK_HZ="$hz" \
            LLVM_PROFILE_FILE="$P/kz-%p.profraw" PAD_FILE="$pad" PS3_VERBOSE=0 AUDIO_RATE=1 PS3RECOMP_VK_WINDOW=0 \
            GCM_PARK_SKIP=1 PS3RECOMP_VK_GUEST_PROGRAMS=1 KZ_LIBSRE_IMAGE="$KZ_WORK/fw/out/libsre_image.bin" \
            timeout -s KILL "${KZ_PGO_ROUTE_MAX:-900}" "$B/KZ1HD" "$KZ_VFS/PS3_GAME/USRDIR/EBOOT.elf" > "$log" 2>&1) &
        run=$!
        # the main menu's background movie (descriptor 2), then Campaign > Select level > level 1 > Normal
        i=0; while [ $i -lt 180 ] && ! grep -aq "descriptor 2:" "$log" 2>/dev/null; do sleep 1; i=$((i + 1)); done
        sleep 5
        for k in 0x0008 0x4000 0x4000 0x4000 0x4000 0x0040_4 0x4000 0x4000; do
            case $k in *_*) echo "${k%_*} ${k#*_}" > "$pad"; sleep 2 ;; *) echo "$k" > "$pad"; sleep 9 ;; esac
        done
        wait "$run" || true
        grep -a "^\[route\] replay ended" "$log" || { echo "pgo.sh: route $n did not replay to its end (log: $log)" >&2; exit 1; }
        grep -aq "^\[kz-pgo\] profile written at quit" "$log" || { echo "pgo.sh: route $n wrote no profile (log: $log)" >&2; exit 1; }
    done
fi
ls "$P"/*.profraw > /dev/null 2>&1 || { echo "pgo.sh: the play wrote no profile" >&2; exit 1; }
llvm-profdata merge -o "$P/kz.profdata" "$P"/*.profraw
rm -f "$P"/*.profraw

quiet "$P/configure.log" cmake -S "$HERE" -B "$BUILD" -DKZ_PGO_USE="$P/kz.profdata"
# the profile is not a dependency ninja knows: rebuild the lifted code with it
touch "$(cache RECOMP_DIR)"/*.cpp
quiet "$P/build.log" ninja -C "$BUILD"

# Did the profile apply? The lifted code builds with -w, which hides clang's warnings about a profile that does not
# match it, so one lifted unit is compiled again (to IR, thrown away) with only those warnings on.
cmd=$(ninja -C "$BUILD" -t commands KZ1HD | grep -m 1 '/ppu_recomp_000\.cpp$') ||
    { echo "pgo.sh: no compile command for ppu_recomp_000.cpp in $BUILD" >&2; exit 1; }
cmd=$(printf '%s\n' "$cmd" | sed -e 's/ -w / /' -e 's/ -MD -MT [^ ]* -MF [^ ]*//' -e 's# -o [^ ]*# -S -emit-llvm -o /dev/null#')
warn=$(cd "$BUILD" && eval "$cmd -Wno-everything -Wprofile-instr-unprofiled -Wprofile-instr-out-of-date" 2>&1) ||
    { printf '%s\n' "$warn" >&2; echo "pgo.sh: the check compile failed" >&2; exit 1; }
if [ -n "$warn" ]; then
    printf '%s\n' "$warn" >&2
    echo "pgo.sh: the profile does not match the lifted code (lifted again since it was made?): run pgo.sh again" >&2
    exit 1
fi
echo "build: $BUILD/KZ1HD with $P/kz.profdata (the profile matches the lifted code)"
