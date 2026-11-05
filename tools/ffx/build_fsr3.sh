#!/bin/bash
# Builds the FidelityFX SDK's FSR 3 upscaler and VK backend into one static library for
# ps3recomp's FSR 3 path (CMake: -DPS3RECOMP_FFX_DIR=<out>), on Linux.
#
#   tools/ffx/build_fsr3.sh <FidelityFX-SDK checkout (v1.1.4)> <out dir>
#
# The SDK's shaders are compiled by its own FidelityFX_SC.exe (Windows only), run here under
# Wine in a prefix of its own. The SDK targets Windows; ffx_linux_compat.h stands in for the
# MSVC library calls it uses, the context sizes grow (wchar_t is 4 bytes here) and the backend's
# scratch arrays are aligned to 64 bytes (clang stores some of their members with aligned SSE).
# Nothing of the SDK is changed in place: the patched files are copies under <out>.
set -eu
SDK=$(cd "$1/sdk" && pwd); OUT=$(mkdir -p "$2" && cd "$2" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
G=$SDK/include/FidelityFX/gpu
mkdir -p "$OUT/perm" "$OUT/obj" "$OUT/src" "$OUT/lib"

# 1. shader permutations, as the SDK's CMake makes them for VK
export WINEPREFIX=${WINEPREFIX:-$OUT/wineprefix} WINEDEBUG=-all
BASE=(-reflection -deps=gcc -DFFX_GPU=1 -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
      -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0 -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
      -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0 -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
      -compiler=glslang -e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1
      "-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}" "-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}"
      "-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}" "-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}"
      "-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}" "-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}")
cd "$SDK"
for f in src/backends/vk/shaders/fsr3upscaler/*.glsl; do
    n=$(basename "$f" .glsl)
    for v in ":0" "_wave64:0" "_16bit:1" "_wave64_16bit:1"; do
        suf=${v%%:*}; half=${v##*:}
        [ -f "$OUT/perm/${n}${suf}_permutations.h" ] && continue
        wine tools/binary_store/FidelityFX_SC.exe "${BASE[@]}" -name="${n}${suf}" -DFFX_HALF="$half" \
            -I"$G" -I"$G/fsr3upscaler" -output="$OUT/perm" "$f" >/dev/null 2>&1 &
        p=$!
        # done when its dependency file is written; wine itself may linger after
        for _ in $(seq 300); do [ -f "$OUT/perm/${n}${suf}_permutations.h.d" ] && break; sleep 1; done
        sleep 2; kill $p 2>/dev/null || true; wineserver -k 2>/dev/null || true
        [ -f "$OUT/perm/${n}${suf}_permutations.h" ] || { echo "no permutations for $n$suf"; exit 1; }
    done
done

# 2. headers: the SDK's, with bigger contexts
rm -rf "$OUT/include"; cp -r "$SDK/include" "$OUT/include"
sed -i 's|#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 \* 128)|#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 512)   /* Linux: wchar_t is 4 bytes */|' \
    "$OUT/include/FidelityFX/host/ffx_types.h"
grep -q "1024 \* 512" "$OUT/include/FidelityFX/host/ffx_types.h"

# 3. the VK backend with its scratch arrays aligned
sed -E -e '/ArraySize *= *FFX_ALIGN_UP/ s/, sizeof\(uint32_t\)\)/, 64)/' \
       -e 's|uint8_t\* pMem = \(uint8_t\*\)\(\(BackendContext_VK\*\)\(backendContext \+ 1\)\);|uint8_t* pMem = (uint8_t*)FFX_ALIGN_UP((uintptr_t)(backendContext + 1), 64);|' \
       -e 's|return FFX_ALIGN_UP\(sizeof\(BackendContext_VK\) \+|return 64 + FFX_ALIGN_UP(sizeof(BackendContext_VK) +|' \
    src/backends/vk/ffx_vk.cpp > "$OUT/src/ffx_vk.cpp"
grep -q "return 64 + FFX_ALIGN_UP" "$OUT/src/ffx_vk.cpp"

# 4. compile
CXX=${CXX:-clang++}
for f in "$OUT/src/ffx_vk.cpp" src/components/fsr3upscaler/ffx_fsr3upscaler.cpp src/backends/shared/ffx_shader_blobs.cpp \
         src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp src/shared/ffx_assert.cpp src/shared/ffx_message.cpp \
         src/shared/ffx_object_management.cpp src/shared/ffx_breadcrumbs_list.cpp; do
    $CXX -std=c++17 -O2 -fPIC -c -include "$here/ffx_linux_compat.h" -DFFX_FSR3UPSCALER -DFFX_GCC \
        -I"$OUT/include" -I"$OUT/include/FidelityFX/host" -Isrc/shared -Isrc/backends/shared -Isrc/backends/vk \
        -Isrc/components -I"$OUT/perm" "$f" -o "$OUT/obj/$(basename "$f" .cpp).o"
done
rm -f "$OUT/lib/libffx_fsr3_vk.a"; ar rcs "$OUT/lib/libffx_fsr3_vk.a" "$OUT"/obj/*.o
echo "built $OUT/lib/libffx_fsr3_vk.a: configure with -DPS3RECOMP_FFX_DIR=$OUT"
