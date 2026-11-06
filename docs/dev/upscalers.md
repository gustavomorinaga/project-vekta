# Temporal upscalers: DLSS and FSR 3.1

ENHANCEMENT: with `PS3RECOMP_TEMPORAL=dlss` or `PS3RECOMP_TEMPORAL=fsr3` the game draws the scene at a lower
resolution than the output (or at the same one, in `native`) and a temporal upscaler makes the final image. The HUD
is drawn afterwards, at the output resolution. Patches: 2045 (the temporal path and DLSS), 2046 (FSR 3.1), 2047 (the
jitter fix and per-object motion), 2048 (sprint motion blur over the upscaled output), 2049 (which upscalers a build
has, for the PC menu), 2050 (per-object motion pairs, on by default), 2051 (FSR 3.1 from a CMake target) and 2052
(both in the Windows exe). In the maintainer's private series they are 0112 to 0118, except 2051
([patch-numbers.md](patch-numbers.md)).

## Using them

| Variable | Values |
|---|---|
| `PS3RECOMP_TEMPORAL` | `dlss` or `fsr3` (unset: nothing changes) |
| `PS3RECOMP_UPSCALE_MODE` | `quality` (default), `balanced`, `performance`, `ultra`, `native` (DLAA, or FSR's native AA) |
| `PS3RECOMP_TEMPORAL_OBJECT_MOTION` | `0` turns per-object motion vectors off (default on) |
| `PS3RECOMP_TEMPORAL_DUMP` | writes each frame's colour, depth, camera, motion and velocity, for offline checks |

In the game the choice is PC > "Temporal Upscaler (on restart)" (the `temporal` key of `kz1hd_pc.ini`: `off`,
`fsr3-<mode>` or `dlss-<mode>`); an upscaler the build does not have shows as "(not built)". Like the internal scale,
it takes effect at the next start, and a window or display-mode change during play also changes the scene resolution
only at the next start.

The output is the size the scaling already gave: the window's with `PS3RECOMP_VK_SCALE_FIT`, or
`PS3RECOMP_VK_SCALE` times 1280x720. The scene is drawn at the size the upscaler asks for that output; Quality at
2560x1440, for example, draws the scene at 1707x960.

## Distribution

The same on Linux and Windows: FSR 3.1 is in every build, and DLSS only where the player accepted NVIDIA's licence.

- **FSR 3.1** is in every build, the Linux program and the Windows exe. The FidelityFX SDK's files it needs (MIT) are vendored in
  `third_party/fidelityfx-fsr3/` with its shaders already compiled, and `port/kz1hd/CMakeLists.txt` builds them into
  the static library `kz_ffx_fsr3`, which ps3recomp links (patch 2051, `PS3RECOMP_FFX_TARGET`): no SDK checkout, no
  Wine. `tools/ffx/build_fsr3.sh <FidelityFX SDK 1.1.4> <out>` is how those files are made again (it runs the SDK's
  Windows-only shader compiler under Wine; `third_party/fidelityfx-fsr3/README.md`). `-DKZ_FSR3=OFF` leaves FSR 3.1
  out; `-DPS3RECOMP_FFX_DIR=<a build_fsr3.sh output>` links that build instead.
- **DLSS** is NVIDIA's, under NVIDIA's DLSS SDK licence, and the project never redistributes it: no NVIDIA file is in
  the repository (`tools/check-tree.sh`) or in a release folder, zip or tarball (`tools/release/assemble_payload.py`
  refuses one by name, by its folder, and by size and SHA-256 of every pinned file). The player's own machine
  downloads it, and only after the player accepts that licence: `tools/install.sh` asks once (with the licence's link
  and its downloaded text), `tools/release/firstrun.py --dlss accept|decline` answers without asking, and the answer
  is kept in `work/dlss-choice`. Accepted, firstrun's `dlss` step fetches the SDK's files from
  `raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/` into `work/toolchain-cache/dlss-sdk/` (laid out as the SDK
  folder), each checked by size and SHA-256 against `tools/release/toolchain.json`'s `file_sets`: the license, the
  headers `rsx_vk_dlss.cpp` reads, and for the build's own OS the NGX library it links (`libnvsdk_ngx.a`, or on
  Windows `nvsdk_ngx_s.lib`) and the runtime (`libnvidia-ngx-dlss.so.310.9.16`, or `nvngx_dlss.dll`). The compile
  then passes `KZ_DLSS_SDK` (`-DPS3RECOMP_DLSS_SDK`) and puts the runtime library beside the program. On Windows the
  setup program asks instead, on a page of its own: a box, unticked by default, "Download NVIDIA DLSS (NVIDIA RTX
  GPUs). I accept the NVIDIA DLSS SDK license", with the licence's link; its answer goes to `firstrun.py --dlss` and
  `work\dlss-choice` and is offered again on an upgrade. `run.cmd` (the zip) asks as `tools/install.sh` does. Declined, nothing is
  downloaded, the build has no DLSS, and the PC menu shows it as "(not built)"; no NVIDIA file is left beside the
  program. NVIDIA DLSS is credited by name (NOTICE.md, the project page); no NVIDIA logo is used.
- By hand: `-DPS3RECOMP_DLSS_SDK=<DLSS SDK folder>` (github.com/NVIDIA/DLSS; 310.9.16 tested) with
  `lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.<version>`, or on Windows `lib/Windows_x86_64/rel/nvngx_dlss.dll`, next
  to the program.

Without DLSS the upscaler is a stub and that choice behaves as off.

## Windows

`tools/build-win.sh` (`KZ_DLSS_SDK=<the SDK>` for DLSS) builds both into `KZ1HD.exe` with the pinned llvm-mingw, and
the setup program and `run.cmd` do the same on the player's PC.

- **FSR 3.1:** the vendored sources build with llvm-mingw as they are. `wchar_t` is 2 bytes on Windows and MinGW has
  the MSVC calls the SDK uses, so `tools/ffx/ffx_mingw_compat.h` only brings in the headers MSVC's own headers
  include by themselves (`<mutex>`, `<cmath>`, `<new>`) and `_countof` for libc++. The bigger default context of the
  vendored `ffx_types.h` (made for Linux) is harmless here: ps3recomp and the library read the same header. The
  scratch memory comes from `_aligned_malloc`, as the Windows C library has no `aligned_alloc` (patch 2052).
  `tools/ffx/build_fsr3.sh <SDK> <out> windows` still makes a separate library for `-DPS3RECOMP_FFX_DIR`, with the
  system clang and MinGW-w64.
- **DLSS:** NVIDIA ships the NGX loader (`nvsdk_ngx_s.lib`) for MSVC only. lld links it; the MSVC runtime libraries
  its objects ask for (`libcmt`, `libcpmt`, `oldnames`) are dropped, and `rsx_vk_ngx_mingw.cpp` (patch 2052) answers
  what it calls from them: `operator new`/`delete` under their MSVC names, the `/GS` stack cookie and its unwind
  handlers, thread-safe local statics, and `std::_Xbad_alloc`/`_Xlength_error`. Its C library calls go to the UCRT,
  and `__CxxFrameHandler3` comes from it too, so the exe needs no `vcruntime140.dll`. The loader also imports
  `d3d12.dll`, `dxgi.dll` and `D3DCompiler_47.dll`, which come with Windows 10 and later.
- **Control Flow Guard:** NGX calls through `__guard_check_icall_fptr` before its indirect calls. With a GCC-built
  MinGW-w64 runtime, the do-nothing check that pointer holds sits in `.00cfg`, which the linker merges into read-only
  data, and the first call faulted. The shim points it at a do-nothing function in code at start-up.
- **Beside the exe:** `nvngx_dlss.dll`, besides the usual DLLs, and only on the machine of a player who accepted
  NVIDIA's licence. The NVIDIA driver brings the rest of NGX.

Measured (Level 7 from a save, 144 Hz mode, headless; fps over the play):

| Date | Build | Runner | Upscaler | fps | What the log said |
|---|---|---|---|---|---|
| 2026-11-04 | private (GCC MinGW-w64), 1280x720 output | Wine 11.21 | none | 117.2 | |
| 2026-11-04 | same | Wine 11.21 | FSR 3.1 Quality, 853x480 to 1280x720 | 105.8 | image right |
| 2026-11-04 | same | Wine 11.21 | DLSS Quality | 119.3 | `NGX init failed (0xBAD00001)`: plain Wine has no NVAPI nor the driver's NGX; the game goes on without an upscaler |
| 2026-11-04 | same | GE-Proton 11 (`PROTON_ENABLE_NVAPI=1`) | DLSS Quality, 853x480 to 1280x720 | 109.1 (with captures) | `DLSS: available`, image right |
| 2026-11-04 | same | GE-Proton 11 | FSR 3.1 Quality | 107.4 | image right |
| 2026-11-05 | this tree (llvm-mingw, `tools/build-win.sh` with `KZ_DLSS_SDK`), 2560x1440 output, 128 s | GE-Proton 11 (`PROTON_ENABLE_NVAPI=1`) | none (2560x1440 native) | 108.6 | |
| 2026-11-05 | same | GE-Proton 11 | FSR 3.1 Quality, 1706x960 to 2560x1440 | 104.7 | `FSR 3: available`, 10,800+ frames upscaled |
| 2026-11-05 | same | GE-Proton 11 (`PROTON_ENABLE_NVAPI=1`) | DLSS Quality, 1707x960 to 2560x1440 | 105.2 | `DLSS: available`, 10,800+ frames upscaled |
| 2026-11-05 | same | Wine 11.21 | FSR 3.1 Quality, 1706x960 to 2560x1440 | 105.8 | `FSR 3: available`, 10,800+ frames upscaled |
| 2026-11-05 | built by the setup program (Windows-hosted llvm-mingw, under Wine), DLSS box ticked | GE-Proton 11 (`PROTON_ENABLE_NVAPI=1`) | DLSS Quality, 1707x960 to 2560x1440 | 107.1 | `DLSS: available`, 10,800+ frames upscaled |
| 2026-11-05 | private, real Windows (RTX 3070 Ti, fullscreen 3440x1440, 144 cap, Reflex boost) | Windows | DLSS Quality, about 13 min | 135.7 | `DLSS: available`, about 93,600 upscaled frames, clean exit; FSR 3.1 worked too |

Proton copies the driver's `nvngx.dll` and `_nvngx.dll` (`/usr/lib/nvidia/wine`) into its prefix and dxvk-nvapi
answers NGX: that is why DLSS runs there and not under plain Wine. On Windows with `scale=75` DLSS outputs 75% of
the screen (2580x1080 from 1720x720) and the scaling stretches the rest; `scale=100` lets it deliver the screen's
resolution.

## How it works

- **Jitter:** the world camera and the first-person model's camera get a sub-pixel offset (Halton 2,3) through an
  off-centre frustum, in `port/kz1hd/native_ppu/camera.cpp`, after `func_001A9FC8`.
- **Each frame's camera:** the unjittered view-projection, the jitter, near, far and FOV go to the backend every
  frame. They come from the view `func_001AA618` builds (the projection at +0x380, the view-projection at +0x310), and
  are taken in the projection hook, so the lift needs no hook of `func_001AA618`. Each frame is tagged with the count
  of flips the game has queued so far, which is the count the RSX has reached when it draws that frame
  (`rsx_temporal.h`). On Level 7, 14,400 of 14,400 frames had the right record.
- **Depth:** the game clears the world's depth before it draws the weapon. The backend copies the depth at that
  clear, and what is drawn after it is the first-person model.
- **Motion vectors:** from the camera. Each pixel goes back through this frame's view-projection and forward through
  the previous one (`rsx_vk_motion.comp`). The weapon does not move on screen.
- **Jitter one frame late:** a frame's draws carry the jitter the camera made one frame earlier, while the
  view-projection is the frame's own, so the upscaler gets the jitter of the previous record (patch 2047). Before
  that, DLSS and FSR were given the wrong jitter.
- **Per-object motion vectors** (on by default; `PS3RECOMP_TEMPORAL_OBJECT_MOTION=0` turns them off): the scene's
  draws use a variant of their pipeline that runs the vertex program again with the constants the same draw had in the
  previous frame, and the fragment program writes the difference to an extra target.
  - **Pairing:** the previous frame's draw is found by the engine's key (program, count and topology; vertex
    addresses change every frame), by material and, among identical instances, by the closest constants in the rows
    the vertex program reads. Comparing other rows picked the wrong instance, hundreds of pixels away (patch 2050).
  - **Safety limit:** an object's motion is used only where its difference from the camera's motion stays below 6 %
    of the frame.
  - **Level 7:** the reprojection error of the previous frame onto the current one falls from 7.76 to 7.24 over the
    whole image and from 10.42 to 8.93 where the motions differ, for about 2 % of the frame rate.
- **Where the upscale happens:** at the display buffer copy the MLAA ran on. The output image stands in for the
  display buffer until it is presented, so the HUD is drawn over it; viewport and scissor follow the target's size.

The conventions were checked this way:

- **Matrix:** reprojecting real frames (colour, depth and matrix) with an offline script, the warp's error fell on
  the frames with motion with y up and z = 2d-1.
- **Jitter sign:** as in the DLSS programming guide, the value is the image's offset, x to the right and y down. FSR
  uses the same convention.

## Results (Level 7 route, unpaced)

| Setting | fps | 1 % low |
|---|---|---|
| 1280x720 native | 131.0 | 89.8 |
| DLSS Quality, 853x480 to 1280x720 | 130.9 | 83.5 |
| DLAA 1280x720 | 130.4 | 77.7 |
| 2560x1440 native | 131.8 | 80.8 |
| DLSS Quality, 1707x960 to 2560x1440 | 130.5 | 68.9 |
| Native (another session, busier machine) | 123.6 | 77.5 |
| FSR 3.1 Quality, 853x480 to 1280x720 (same session) | 120.9 | 76.5 |

The game is still bound by the PPU main thread, so the upscaler does not change the frame rate here. What it gives
is image quality at high resolutions, or frame rate on slower GPUs.

## What is missing

- **Moving objects:** per-object motion vectors improve the average, but on some frames they are still worse than the
  camera's, and objects animated outside the constants (vertices already transformed) only get the camera's motion.
- **Comparing by image:** routes diverge between runs, so an image A/B does not hold. To measure, use
  `PS3RECOMP_TEMPORAL_DUMP` and reproject offline.
- **Sprint motion blur:** runs over the upscaler's output since patch 2048. The image stages work on a 1280x720 copy
  and the final blend comes out at the output resolution. The MLAA does not run, since the upscaler already does the
  antialiasing.

## FSR 4

Not implemented. Why, and the way in for when it can be tested:

- **Platform:** FSR 4 (AMD's machine-learning upscaler) ships through FidelityFX SDK 2.x / the AMD FidelityFX API as a
  signed DLL (`amd_fidelityfx_dx12.dll` and the `amd_fidelityfx_loader`), for Windows and DirectX 12 only, and runs
  only on Radeon RX 9000 (RDNA 4) GPUs. There is no Vulkan or Linux version; on Linux it exists only as the Windows
  DLL under Proton/vkd3d-proton.
- **Backend:** this port draws with Vulkan, also in the Windows program. FSR 4 would need Vulkan to DirectX 12 interop
  (shared images and a DirectX 12 device for the upscaler alone, with fences between the two) or ps3recomp's D3D12
  backend, which has none of the temporal passes today.
- **Testing:** the maintainer's GPU is NVIDIA, so none of it can be run or compared here.
- **The way in, with the hardware:** the same input as DLSS and FSR 3.1 (`rsx_vk_dlss.h`: colour, R32F depth, motion
  vectors in render pixels, jitter in the same convention, reset), a new provider behind `PS3RECOMP_TEMPORAL=fsr4`
  and its row in the PC menu. The rest (jitter, per-object motion vectors, HUD at the output, motion blur) already
  serves.
- **Today:** on AMD cards FSR 3.1 is the temporal upscaler, on any GPU with Vulkan.
