#!/bin/sh
# Generates everything the port compiles from your own files (tools/env.sh): the PPU lift of the EBOOT, its SPU
# images, policy module and job modules, the firmware's MP3 decoder and the firmware libsre. Output in $KZ_WORK/lift.
# Run tools/ps3recomp-patched.sh first, and again after changing port/patches (the lifters are patched too).
set -e
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
T="$PS3RECOMP_DIR/tools"; P="$KZ_ROOT/port/kz1hd"; K="$KZ_WORK/lift"; E="$KZ_EBOOT"
[ -f "$T/ppu_lifter.py" ] || { echo "no $T/ppu_lifter.py: run tools/ps3recomp-patched.sh first" >&2; exit 1; }
[ -f "$E" ] || { echo "no EBOOT at $E (KZ_EBOOT; docs/GETTING-FILES.md)" >&2; exit 1; }
mkdir -p "$K/analysis"

echo "== PPU"
# The loader's view of the EBOOT (segments, imports, the functions its tables name), then the function list from
# those and the seeds in re/function_seeds.txt.
"$KZ_PY" "$T/ppu_loader.py" "$E" -o "$K/analysis/" > /dev/null
grep -v '^#' "$KZ_ROOT/re/function_seeds.txt" | "$KZ_PY" -c 'import json, sys; json.dump([l.strip() for l in sys.stdin if l.strip()], sys.stdout)' \
    > "$K/analysis/seeds.json"
"$KZ_PY" "$T/find_functions.py" "$E" --seed-json "$K/analysis/EBOOT.functions.json" --seed-json "$K/analysis/seeds.json" \
    --output "$K/analysis/functions.json" > /dev/null
# Functions port/kz1hd/native_ppu/ reimplements: the lifter emits them as <name>_lifted.
PPU_LIFT_NATIVE=$(grep -ho '^void func_[0-9A-F]*(ppu_context\* ctx)$' "$P"/native_ppu/*.cpp 2>/dev/null |
    sed 's/^void //; s/(.*//' | paste -sd, -)
export PPU_LIFT_NATIVE
# 0x3B5CB0/0x3B5DD4: its libc setjmp/longjmp, which Lua 4.0 raises its errors with.
"$KZ_PY" "$T/ppu_lifter.py" "$E" --functions "$K/analysis/functions.json" --hle-stubs "$K/analysis/EBOOT.imports.json" \
    --setjmp 0x3B5CB0 --longjmp 0x3B5DD4 --code-end 0x52f740 --chunk-lines 150000 -j "${KZ_JOBS:-$(nproc)}" --output "$K/gen/" > "$K/lift.log"
tail -2 "$K/lift.log"
# Guest registers in host locals inside each function (see the tool's header): ~+7% in Level 1 at the 144 Hz clock.
"$KZ_PY" "$T/ppu_regcache.py" "$K/gen" | tail -1

echo "== SPU"
# Functions port/kz1hd/native/ reimplements: the lifter emits them as <name>_lifted.
SPU_LIFT_NATIVE=$(grep -ho '^void [a-z0-9_]*_spu_func_[0-9A-F]*(spu_context\* ctx)$' "$P"/native/*.c 2>/dev/null |
    sed 's/^void //; s/(.*//' | paste -sd, -)
export SPU_LIFT_NATIVE
rm -rf "$K/spu" && mkdir -p "$K/spu/images"
"$KZ_PY" "$T/extract_spu_images.py" "$E" --output "$K/spu/images" > /dev/null
"$KZ_PY" "$KZ_ROOT/tools/extract_spu_modules.py" "$E" --out "$K/spu" --pm 0x5B9180:12160 --ps3recomp "$PS3RECOMP_DIR" | tail -1
"$KZ_PY" "$T/lift_jobmods.py" --jobmods "$K/spu/jobmods" --out "$K/spu" --register "$K/spu/jobmods_register.c" \
    --symbol-prefix kz1 | tail -1
"$KZ_PY" "$T/build_spu_workloads.py" --images "$K/spu/images" --lifted "$K/spu" --out "$K/spu/spu_workloads.c" \
    --register-fn kz1_spu_register_all --constructor --title kz1hd | tail -1

# The firmware's MP3 decoder, which MultiStream (SPU image 3) loads from dev_flash at LS 0x1A880 for the music:
# lifted at that base, registered in stubs.cpp.
D="$PS3_DEV_FLASH"
if [ -f "$D/sys/external/flashMP3.pic" ]; then
    mkdir -p "$K/spu/flashmp3" "$K/spu/flashmp3_src"
    "$KZ_PY" "$T/rebase_spu_elf.py" "$D/sys/external/flashMP3.pic" "$K/spu/flashmp3_src/flashMP3_1A880.elf" 0x1A880 > /dev/null
    "$KZ_PY" "$T/spu_lifter.py" --auto-functions "$K/spu/flashmp3_src/flashMP3_1A880.elf" --symbol-prefix kzmp3_ \
        --output "$K/spu/flashmp3" | grep 'function(s) lifted'
else
    echo "no $D/sys/external/flashMP3.pic (PS3_DEV_FLASH): the game's music and sound stop at the first MP3"
fi

echo "== libsre"
# The firmware's libsre runs for the two SPURS LFQueue calls the runtime does not reimplement (stubs.cpp):
# its image, function list and imports from lift_prx.py, the imports in the lifter's --hle-stubs form, then the lift.
F="$KZ_WORK/fw"
[ -f "$KZ_LIBSRE" ] || { echo "no libsre.prx at $KZ_LIBSRE (KZ_LIBSRE; docs/GETTING-FILES.md)" >&2; exit 1; }
mkdir -p "$F/out"
"$KZ_PY" "$T/lift_prx.py" "$KZ_LIBSRE" --base 0x02000000 --output "$F/out/" > /dev/null
"$KZ_PY" -c 'import json, sys
imports = json.load(open(sys.argv[1]))
json.dump([{"library": lib, "nid": nid, "stub": stub} for lib, nids in imports.items() for nid, stub in nids.items()],
          open(sys.argv[2], "w"), indent=1)' "$F/out/libsre_imports.json" "$F/out/libsre_hle_stubs.json"
"$KZ_PY" "$T/ppu_lifter.py" "$F/out/libsre_image.bin" --raw --base 0x02000000 --toc 0x02039AB0 --code-end 0x0201DD4C \
    --functions "$F/out/libsre_functions.json" --hle-stubs "$F/out/libsre_hle_stubs.json" --symbol-prefix libsre_ \
    --header-name libsre_recomp.h --source-name libsre_recomp --output "$F/gen/" > "$F/lift.log"
tail -1 "$F/lift.log"
