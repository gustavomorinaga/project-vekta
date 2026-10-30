#!/bin/sh
# MLAA regression checks (no game run; needs the captures in MLAA_WORK, see README.md):
# builds the harness, the C reference CLI and the GPU tester, then check_mlaa.py.
# Usage: tools/mlaa_harness/regress_mlaa.sh
set -u
H=$(cd "$(dirname "$0")" && pwd)
W=${MLAA_WORK:-$H/../../work/mlaa_cap}
R=${PS3RECOMP_DIR:-$H/../../work/ps3recomp-patched}
mkdir -p "$W/hb"
"$H/build.sh" "$W/hb" || { echo "FAIL harness build"; exit 1; }
cc -O2 -o "$W/hb/mlaa_ref_cli" "$H/mlaa_ref_cli.c" "$H/../../port/kz1hd/native/mlaa_ref.c" || { echo "FAIL mlaa_ref build"; exit 1; }
cc -O2 -o "$W/hb/mlaa_vk_test" "$H/mlaa_vk_test.c" -lvulkan || { echo "FAIL mlaa_vk_test build"; exit 1; }
glslangValidator -V --target-env vulkan1.1 "$R/libs/video/vulkan/rsx_vk_mlaa.comp" -o "$W/hb/mlaa.spv" >/dev/null || { echo "FAIL shader"; exit 1; }
cd "$H" && MLAA_WORK="$W" python3 check_mlaa.py
