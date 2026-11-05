# AMD FidelityFX SDK: the FSR 3.1 upscaler, as this port builds it

The files of AMD's FidelityFX SDK that the FSR 3.1 upscaler and its Vulkan backend need, and the SPIR-V of its
shaders, so the Linux build and the Windows exe have FSR 3.1 with no SDK checkout, no Wine and no prebuilt library: `port/kz1hd/CMakeLists.txt`
compiles them into the static library `kz_ffx_fsr3`, and ps3recomp's FSR 3 path links it (patch 2051,
`PS3RECOMP_FFX_TARGET`). See [docs/dev/upscalers.md](../../docs/dev/upscalers.md).

- **Source:** <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>, tag `v1.1.4`, commit
  `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`, folder `sdk/`.
- **Licence:** MIT, (c) Advanced Micro Devices, Inc. ([LICENSE.txt](LICENSE.txt), also
  [licenses/AMD-FidelityFX-SDK-LICENSE.txt](../../licenses/AMD-FidelityFX-SDK-LICENSE.txt)).

## What is here

| Path | What it is |
|---|---|
| `include/FidelityFX/` | the SDK's headers that the library and `libs/video/rsx_vk_fsr3.cpp` include, unchanged but one: `host/ffx_types.h` has `FFX_SDK_DEFAULT_CONTEXT_SIZE` at 512 KiB instead of 128 KiB (`wchar_t` is 4 bytes on Linux, so the contexts are bigger) |
| `src/` | the SDK's sources the library compiles, at their places in `sdk/src/`, unchanged but one: `backends/vk/ffx_vk.cpp` aligns its scratch arrays to 64 bytes and reserves 64 bytes more (clang stores some of their members with aligned SSE) |
| `perm/` | the shader permutation headers: for each FSR 3 pass and each of its variants (wave64, 16-bit), the SPIR-V blob and its reflection tables (resource names, bindings, counts and sets), with the permutation key and lookup tables |

The library is compiled with `tools/ffx/ffx_linux_compat.h` forced in on Linux (it stands in for the MSVC library
calls the SDK uses) and `tools/ffx/ffx_mingw_compat.h` on Windows (llvm-mingw: only the headers MSVC's bring in by
themselves, and `_countof`), `-DFFX_FSR3UPSCALER -DFFX_GCC`, C++17. The 512 KiB context is harmless on Windows, where
`wchar_t` is 2 bytes: the library and ps3recomp read the same header.

Only the files the compiler reads are kept: the list comes from the compiler's own dependency output (`-MM`) for the
eight library sources and `rsx_vk_fsr3.cpp`.

## Making them again

`tools/ffx/build_fsr3.sh <FidelityFX-SDK checkout at v1.1.4> <out>` makes `<out>/perm`, `<out>/include` and
`<out>/src/ffx_vk.cpp` the way these were made: the permutations by the SDK's own shader compiler
(`sdk/tools/binary_store/FidelityFX_SC.exe`, with `-compiler=glslang` and the options of the SDK's CMake for VK), run
under Wine in a prefix of its own, and the two adapted files by `sed`. To update this folder, run it, copy the files
that the dependency list names from `<out>` and the SDK into the same places here (the `.h.d` dependency files are not
kept), and build.
