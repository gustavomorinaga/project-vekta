#!/bin/sh
# Checks a Linux release folder (tools/release/assemble_payload.py --platform linux) with no game: its bundled
# toolchain builds the libraries (build_linux_deps.py) and ps3recomp's own shader test and boot-smoke program from the
# folder's patched tree, the shader test passes, and nothing it built needs a glibc newer than 2.31. Run it where
# there is no compiler (release.yml runs it in a Debian 11 container with only Python); it writes only in the
# folder's work/. Exit 0 = ok, 1 = a check failed, 2 = usage.
#   tools/release/check_linux_folder.sh FOLDER
set -eu
[ $# = 1 ] && [ -f "$1/run.sh" ] || { echo "usage: check_linux_folder.sh FOLDER (a Linux release folder)" >&2; exit 2; }
f=$(cd "$1" && pwd)
w="$f/work"
export KZ_LINUX_TOOLCHAIN=pinned KZ_WORK="$w" KZ_ZIG="$w/toolchain-cache/zig/zig"
python3 "$f/tools/release/build_linux_deps.py" --cache "$w/toolchain-cache" --prefix "$w/linux-deps"
export PATH="$w/toolchain-cache/cmake-linux/bin:$w/toolchain-cache/ninja-linux:$PATH"
b="$w/check-build"
cmake -G Ninja -S "$w/ps3recomp-patched" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$f/port/cmake/zig-linux.cmake" \
  -DKZ_LINUX_DEPS="$w/linux-deps" -DCMAKE_BUILD_TYPE=Release -DPS3RECOMP_RSX_VULKAN=ON \
  -DPS3RECOMP_VULKAN_INCLUDE_DIR="$w/linux-deps/include" -DPython3_EXECUTABLE="$(command -v python3)" \
  -DCMAKE_SKIP_BUILD_RPATH=ON > "$w/check-configure.log" 2>&1 || { tail -30 "$w/check-configure.log"; exit 1; }
grep -q "guest programs available" "$w/check-configure.log" || { echo "FAIL: glslang was not found"; exit 1; }
cmake --build "$b" --target test_shader_spirv ps3recomp_boot_smoke > "$w/check-build.log" 2>&1 ||
  { grep -E "error|FAILED" "$w/check-build.log" | head -30; exit 1; }
"$b/test_shader_spirv" | tail -3
for l in libavcodec.so.63 libavformat.so.63 libavutil.so.61 libswscale.so.10 libswresample.so.7; do
  [ -f "$w/linux-deps/ffmpeg/$l" ] || { echo "FAIL: no $l staged"; exit 1; }
done
echo "check_linux_folder: built with the bundled toolchain: $b/test_shader_spirv $b/ps3recomp_boot_smoke"
# The boot smoke: ps3recomp's hand-written title, lifted and run (no game, no GPU)
( cd "$b" && timeout 120 ./ps3recomp_boot_smoke smoke/smoke.elf ) > "$w/check-smoke.log" 2>&1 && rc=0 || rc=$?
tail -5 "$w/check-smoke.log"
[ "$rc" = 0 ] || { echo "FAIL: the boot smoke exited $rc"; exit 1; }
echo "check_linux_folder: ok"
