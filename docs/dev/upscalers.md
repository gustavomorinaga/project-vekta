# Temporal upscalers: DLSS and FSR 3.1

ENHANCEMENT: with `PS3RECOMP_TEMPORAL=dlss` or `PS3RECOMP_TEMPORAL=fsr3` the game draws the scene at a lower
resolution than the output (or at the same one, in `native`) and a temporal upscaler makes the final image. The HUD
is drawn afterwards, at the output resolution. Patches: 2045 (the temporal path and DLSS), 2046 (FSR 3.1), 2047 (the
jitter fix and per-object motion), 2048 (sprint motion blur over the upscaled output), 2049 (which upscalers a build
has, for the PC menu) and 2050 (per-object motion pairs, on by default). In the maintainer's private series they are
0112 to 0117 ([patch-numbers.md](patch-numbers.md)).

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

No SDK is in the repository. To build them in:

- **DLSS:** configure with `-DPS3RECOMP_DLSS_SDK=<DLSS SDK folder>` (github.com/NVIDIA/DLSS; 310.9.16 tested) and put
  `lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.<version>` next to the program. On Windows the file is
  `nvngx_dlss.dll`.
- **FSR 3.1:** run `tools/ffx/build_fsr3.sh <FidelityFX SDK 1.1.4> <out>` and configure with
  `-DPS3RECOMP_FFX_DIR=<out>`. The script compiles the shaders with the SDK's own compiler, which exists only for
  Windows, under Wine in a prefix of its own, and adapts the SDK's Windows code to Linux without changing the SDK in
  place (`tools/ffx/ffx_linux_compat.h`).

Without either option the upscaler is a stub and the program behaves as before.

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
