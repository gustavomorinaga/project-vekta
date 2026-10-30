# Plan: a native Killzone trilogy (Windows and Linux, Vulkan) by static recompilation

Researched on 2026-10-13. Priority: Killzone HD (the first Killzone), then Killzone 2, then Killzone 3. This is the
plan as written then; what happened since is in [port-journal.md](port-journal.md) and the [roadmap](../ROADMAP.md).

## 1. Goal and principles

Native Windows and Linux versions of the three games, rendering with **Vulkan** (DirectX later, if at all), with
modern PC options and fine control over the game.

1. **Fidelity first.** The first release of each game is the original running natively, with no differences in logic,
   animation, character positions or textures. Improvements come after.
2. **The game's code is the original.** No rewriting game systems by hand: the EBOOT's PPU and SPU code is translated
   automatically; manual work stays in the runtime (the PS3's system, the GPU) and in well-bounded extension points.
3. **One base for the three games.** Killzone HD, 2 and 3 use the same Guerrilla engine lineage (SPURS, Havok on SPU,
   Edge, a deferred renderer in Killzone 2 and 3). Runtime, renderer and options layer are shared; each game has only
   its own "map" (where its camera, post-processing, tone mapping, HUD and input are).
4. **Nothing of the game in git.** The EBOOT, code generated from it, keys and data stay out of the repository. The
   player provides their own copy.

## 2. Why change strategy

Before this plan the project was a hand-written reimplementation in Rust (about 10,000 lines), guided by static
analysis and footage. It got the data formats right, but each game system had to be rediscovered, and the
differences (textures, positioning, animation) followed directly: the result only approached the original. RPCS3 was
no base either, because it drew Killzone HD's 3D black and allows none of the wanted improvements.

Static recompilation solves fidelity at the root: the logic that runs is Guerrilla's.

## 3. State of the art (sources)

- **N64: Recompiled** (MIT): N64 binaries to C, graphics through RT64; the base of Zelda64Recomp. It made the "1:1
  translation + runtime + overrides" model popular. [Software Engineering Daily](https://softwareengineeringdaily.com/2024/10/02/n64-recompiled-with-dario-and-wiseguy/),
  [Zelda Dungeon](https://www.zeldadungeon.net/majoras-mask-native-pc-port-now-available-thanks-to-new-recompilation-tool/).
- **XenonRecomp**: the same for the Xbox 360's PowerPC. Functions take the CPU state and the memory base; loads and
  stores swap endianness; indirect calls go through a function table after the image; jump tables by patterns. No
  runtime. [README](https://cdn.jsdelivr.net/gh/hedge-dev/XenonRecomp@main/README.md).
- **UnleashedRecomp** (GPL-3.0): a complete port of Sonic Unleashed with XenonRecomp, shaders translated to HLSL, its
  own D3D12/Vulkan renderer. A reference for how a port with PC options is organised. [GitHub](https://github.com/hedge-dev/UnleashedRecomp).
- **ps3recomp** (sp00nznet, MIT, v0.14.0 in October 2026): the equivalent for the PS3. A Python pipeline
  (`tools/ppu_loader.py`, `ppu_lifter.py`, `spu_lifter.py`, `extract_spu_images.py`, `gen_hle_nids.py`), a C/C++ runtime
  (`runtime/ppu`, `runtime/spu`, `runtime/memory/vm.h`, about 93 HLE modules in `libs/`). SPU: 256 KB of local store per
  virtual SPU, DMA as `memcpy`, channels as queues, dispatch by `image_id`. RSX: cellGcm, a common draw engine, D3D12,
  Metal and Vulkan backends (the last optional, with no game running on it in the main tree yet).
  [GitHub](https://github.com/sp00nznet/ps3recomp), [Generation Amiga](https://www.generationamiga.com/2026/06/15/ps3recomp-the-new-tool-bringing-native-playstation-3-ports-closer-to-pc/).
- **Killzone 3 on ps3recomp**: a community port reached the first level with geometry, lighting, shadows and depth of
  field (a Windows build under Proton). Its author proposed about 60 generic fixes (VMX, TLS, SPU `lqa`/`stqa`, the MFC
  table, SPURS Taskset2, the cellGcm IO map, fragment programs, FP16/FP32, EDAT), accepted by the maintainer as separate
  PRs on 2026-10-13. [Issue #214](https://github.com/sp00nznet/ps3recomp/issues/214). (It became Project Helghast.)
- **RPCS3** (GPL-2.0): a reference for LV2, SPURS and RSX behaviour, and for internal resolution scaling (a surface
  cache that resizes render targets and handles memory reinterpretation). Do not copy code.
  [High Resolution Rendering](https://rpcs3.net/blog/?p=1015), [the surface cache report](https://rpcs3.net/blog/?p=1701).
- **The game's context**: Killzone HD (2012) was redone with Guerrilla's involvement.
  [PlayStation Blog](https://blog.playstation.com/2012/09/27/killzone-hd-re-developer-interview-part-1/). SPU use in
  Killzone 2: [Guerrilla, GDC 2009](https://www.guerrilla-games.com/read/the-playstation-3s-spus-in-the-real-world-a-killzone-2-case-study).

## 4. Language and base

- **Base:** ps3recomp, in a port repository of our own (this one), with ps3recomp as a pinned submodule. It already
  solves the generic parts (lifters, LV2, SPURS, cellGcm), and Killzone 3 runs on it.
- **Language:** C++20 in the port, because the generated code and ps3recomp's runtime are C/C++ and fixes need to go
  back upstream. Rust is not on the critical path; it may come back for isolated tools (launcher, packaging, asset
  conversion), with no FFI in the game loop.

## 5. Architecture

```
the player's decrypted EBOOT.elf ──ppu_loader/ppu_lifter──> lifted PPU C++   (generated, outside git)
embedded SPU images ──extract_spu_images/spu_lifter──────> lifted SPU C     (generated, outside git)
                                   │
ps3recomp (submodule) ─ PPU/SPU runtime, vm, LV2, HLE libraries, cellGcm → draw engine
                                   │
the port (this repository, C++) ─ its own Vulkan backend + the PC layer:
   ├─ window, swapchain, display modes, HDR, VRR, limiter, Reflex   (SDL + Vulkan)
   ├─ internal resolution scale of the RSX surfaces
   ├─ injected post-processing: upscalers, sharpening, HDR tone mapping
   ├─ input: SDL gamepad → cellPad; mouse and keyboard straight into the game's camera
   ├─ options and an overlay menu for what the game lacks
   └─ each game's map: camera, post passes, tone mapping, HUD, simulation step
                                   │
the player's decrypted archive, read through cellFs
```

The key to the improvements is each game's **frame map**: from RenderDoc captures, the HDR scene target, the depth
buffer, the tone-mapping pass, each post pass (DoF, motion blur, grain, flare), the HUD pass and the camera constants
(view/projection) the game sends to the RSX. With that the backend knows where to inject an upscaler, HDR and toggles
without touching the game's logic.

## 6. PC options: feasibility

Key: **R** = in the runtime, valid for the three games; **M** = needs the map or an engine override in each game;
**N** = a feature the original games lack (leaves fidelity). Changes against the original wish list are in the
"Decision" column.

### Display and performance

| Option | Type | Decision |
|---|---|---|
| Display mode: fullscreen, borderless window, window | R | Keep. |
| Output resolution: the monitor's list | R | Keep, listing what the monitor reports. |
| Internal resolution scale: 50 % to 200 % | R | **New.** Separates render resolution from output, as RPCS3 does; real sharpness above 720p. |
| Refresh rate: the monitor's values | R | Keep. |
| V-Sync: on, off | R | Keep. Add automatic **VRR** (G-Sync/FreeSync) when available. |
| Frame-rate limit: off, 30, 60, 75, 90, 120, 144, custom | M | Keep, but above 30 it depends on decoupling the game's simulation step (the originals run at 30 or less). A phase of its own; 30 fixed until then. |
| NVIDIA Reflex: off, on, on + boost | R | Keep, through `VK_NV_low_latency2` (native in Vulkan, Windows and Linux). On AMD/Intel, the open low_latency_layer implements the same extension. [Phoronix](https://www.phoronix.com/news/Low-Latency-Layer) |
| HDR, HDR brightness, HDR paper white, tone mapping | M | Keep. Needs each game's HDR buffer before tone mapping, and the tone mapping replaced. Output through `VK_EXT_swapchain_colorspace` + `VK_EXT_hdr_metadata`; on Linux it depends on the compositor (KDE Plasma 6 or gamescope). |
| Quick preset | R | Keep. |

### Upscaling and antialiasing

Temporal upscalers need depth, motion vectors and camera jitter. The games produce no motion vectors: camera vectors
come from depth reprojection; character vectors need each draw's previous transform kept. The hardest part of the PC
layer.

| Option | Type | Decision |
|---|---|---|
| AMD FSR (Super Resolution) | R/M | Keep, in two steps: spatial FSR 1 first (needs only the image), temporal FSR 3.1 later. Open source, with Vulkan. [GPUOpen](https://gpuopen.com/learn/amd_fsr_3_1_release/) |
| FSR frame generation | M | Keep, after temporal FSR. Works on any GPU. |
| DLSS Super Resolution, DLAA | M | Keep, with temporal FSR (same inputs). Works on an RTX 3070 Ti. |
| DLSS frame generation | M | **Postpone.** RTX 40 or newer only, so it cannot be tested on a 3070 Ti; the Vulkan path on Linux still has known problems. [Streamline #127](https://github.com/NVIDIA-RTX/Streamline/issues/127) |
| DLSS ray reconstruction | N | **Remove.** For ray tracing, which the games do not use. |
| Intel XeSS | R/M | **Postpone.** A closed SDK with Windows binaries only. [Phoronix](https://www.phoronix.com/news/Intel-XeSS-3-SDK-GitHub) |
| Dynamic scale, minimum, maximum, target fps | R | Keep, after the internal scale. |
| Sharpness 0.00 to 1.00 | R | Keep (RCAS/CAS). |
| Anisotropic filtering 1x to 16x | R | Keep. The cheapest improvement: swaps the sampler in the RSX translation. |

### Graphics quality

| Option | Type | Decision |
|---|---|---|
| Texture quality | N | **Replace** with "Textures: original / external pack". The games have one texture level; HD packs would be mods. |
| Field of view 70 to 100 | M | Keep, as horizontal FOV at 16:9, the original as default. A camera override per game. |
| Film grain, chromatic aberration, depth of field, lens flare | M | Keep, each option shown only for the game that has the effect (Killzone HD has less post-processing than 2 and 3). |
| Motion blur: off, reduced, original | M | Keep with these names, because "high" is the original. |
| Shadows (local, cascade, distant) | M | **Merge** into "Shadow resolution: original / high / ultra" and "Shadow distance: original / long". The games do not have that many separate kinds. |
| Volumetric fog and clouds | N | **Replace** with "Full-resolution particles and fog". Killzone 2 draws those effects at reduced resolution; there are no volumetrics. |
| Maximum dynamic decals | M | Keep, if the engine's limit is found. |
| Screen-space reflections, subsurface scattering, contact shadows, enhanced facial lighting, mirror quality | N | **Remove.** Not in the games; they would be new effects. May come back as improvements after parity. |
| Ambient occlusion | N | **Remove** for now, for the same reason. |
| Colour precision: original, high | R | Keep as experimental: 8-bit render targets promoted to 16 bits against banding. |
| Level of detail: original, high, maximum | M | Keep, as a multiplier of the engine's LOD distance. |

### Controls, audio and others (additions)

| Option | Type | Decision |
|---|---|---|
| Controls: keyboard + mouse, controller | M | Keep. Controllers through SDL (DualShock/DualSense/Xbox, the right icons). Mouse straight into the camera, without the stick's acceleration or dead zone. |
| Sensitivity, invert Y, key remapping | M | **New**, following from mouse and keyboard. |
| HUD scale and safe area | M | **New.** Needed at resolutions and aspect ratios above 16:9. |
| Ultrawide 21:9 and 32:9 | M | **New, later.** Depends on camera and HUD. |
| Sound output: stereo, 5.1, 7.1 | R | **New.** The games already mix multichannel in `cellAudio`. |
| Voice and subtitle language | R | **New.** The language packs are in the archive already. |
| Skip opening movies | R | **New.** |

Fine control of the game comes from the same extension points: overrides of the game's functions (ps3recomp's
`--overrides`), a debug console or menu and, later, a mod API.

## 7. Phases

Each phase has a checkable exit criterion. Phases 1 to 6 are fidelity; 7 to 9, PC.

**Phase 0. Environment.** `cmake`, `ninja`, `mingw-w64-gcc` (the Windows build from Linux), SDL, the Vulkan SDK
(validation layers), RenderDoc, Tracy. ps3recomp at a pinned version. Run `ps3recomp_host` (test scenes without a game)
on the Vulkan backend, on Linux and in the Windows build (Wine + winevulkan, and a Windows machine when there is one).
Exit: the host scenes match on both systems.

**Phase 1. Translate Killzone HD's PPU.** `ppu_loader.py` on the EBOOT; compare the functions found with the about
22,800 found by static analysis and the 269 named imports. `ppu_lifter.py --hle-stubs` and `gen_hle_nids.py`. Exit: it
compiles and links; functions left untranslated are listed and explained.

**Phase 2. Boot to the main loop.** Register each missing NID or syscall (181 direct `sc` instructions in the EBOOT) and
implement the minimum. Mount the decrypted archive. Exit: the game reads `KillzonePS3.ini` and the interface lumps and
reaches its loop.

**Phase 3. SPU and SPURS.** Extract and translate the SPU images (SPURS kernel, policies, jobs, Havok). Bring in the
Killzone 3 fixes if they are not upstream yet. Exit: one frame's jobs complete with the same result as an RPCS3 trace.

**Phase 4. RSX on Vulkan.** `cellGcmSys` (37 imports) + `cellResc` (17) up to the menu. Compare with RPCS3, which draws
the 2D correctly. Exit: the menu drawn and navigable on Linux and Windows.

**Phase 5. First level.** `Level.1` up to player control, with correct 3D (where RPCS3 fails). Compare with reference
footage. Exit: Level 1 playable up to the first checkpoint, without visible differences.

**Phase 6. The full game.** `cellSail`/`cellPamf` (44 + 2 imports) for the movies, with FFmpeg at run time (until then,
a stub that skips the movie). `cellAudio`, saves, trophies as a local log. Exit: the full campaign on both systems.

**Phase 7. PC, level 1 (runtime).** Display modes, output resolution, internal scale, V-Sync/VRR, Reflex, anisotropic
filtering, FSR 1 and sharpening, SDL gamepads, multichannel audio, languages, an options menu. Exit: each option tested
in every Killzone HD level.

**Phase 8. PC, level 2 (the game's map).** Killzone HD's frame map; FOV; mouse and keyboard; post toggles; HUD; HDR;
60+ fps with a decoupled simulation step; then motion vectors and jitter for FSR 3.1, DLSS and DLAA; FSR frame
generation. Exit: the options of section 6 marked M working in Killzone HD.

**Phase 9. Killzone 2 and Killzone 3.** Repeat phases 1 to 6 and 8 for Killzone 2 (about 48,000 functions) and
Killzone 3 (the community port shows the path exists). The runtime and the PC layer are ready by then; the work is each
game's map.

In every phase, generic fixes go back to ps3recomp as PRs; what is Killzone-only stays in this repository.

## 8. Tools

| Tool | Use |
|---|---|
| Ghidra (headless) | Functions, vtables, imports. |
| RPCS3 | A behaviour reference and traces (SPU, RSX, LV2). |
| RenderDoc | Vulkan captures for the frame map and to compare with RPCS3. |
| Vulkan validation layers, Tracy | Correctness and performance. |
| SDL, FFmpeg, FidelityFX SDK, DLSS SDK | Window/input, movies, upscalers. |

## 9. Risks

- **The maturity of ps3recomp and its Vulkan backend.** Upstream did not yet run games on Vulkan in the main tree.
  Mitigation: the Vulkan backend becomes ours (patches in this repository), with ps3recomp pinned, and fixes go
  upstream.
- **Performance.** Tornado Outbreak ran at 10 to 16 fps on ps3recomp; Killzone is heavier on SPU. Mitigation: measure in
  phase 3 and prioritise the Killzone 3 port's optimisations.
- **60+ fps.** Physics, animation and scripts may depend on the fixed step. It may need frame interpolation instead of
  simulating faster. (It turned out that a 60 Hz game clock works: [port-journal.md](port-journal.md).)
- **Motion vectors.** Without them, temporal upscalers leave trails on characters. Hence FSR 1 first.
- **Resolution scale.** The RSX reinterprets memory between formats; scaling render targets needs a careful surface
  cache, as in RPCS3.
- **Licences.** ps3recomp and FidelityFX are MIT; DLSS has NVIDIA's own licence; RPCS3 (GPL-2.0) and UnleashedRecomp
  (GPL-3.0) as references only.

## 10. Decisions taken (2026-10-14)

- The cuts and swaps of section 6 approved.
- **DLSS:** Super Resolution and DLAA go in natively, on Linux and Windows, through the DLSS SDK in Vulkan (no Proton).
  Only DLSS frame generation is postponed: it uses the same inputs and comes when an RTX 40 or newer is available.
- **Linux without Proton:** the Linux build is a native executable (ELF) with Vulkan. Proton and Wine only test the
  Windows build from Linux.
- **Multiplayer (phase 10, after the three campaigns):** Killzone HD has no multiplayer (the EBOOT imports only
  `sceNp`/`sceNpTrophy`, with no network or matchmaking modules; only strings are left). Killzone 2 and 3 have offline
  bot modes, which come with the recompilation, and online modes, whose PSN/Guerrilla servers were shut down. Online
  needs `sceNp*`/matchmaking replaced with a server of our own, as ps3recomp does for the Simpsons Arcade's online.
- **Distribution:** the project publishes only tools, runtime and the PC layer. Each player needs their own copy of the
  games: an installer reads the player's files, translates the EBOOT and compiles on their machine, and the game's data
  is read from the player's copy. Keys are never distributed.

## 11. References

- ps3recomp: https://github.com/sp00nznet/ps3recomp
- The Killzone 3 port and its fixes: https://github.com/sp00nznet/ps3recomp/issues/214
- XenonRecomp: https://cdn.jsdelivr.net/gh/hedge-dev/XenonRecomp@main/README.md
- UnleashedRecomp: https://github.com/hedge-dev/UnleashedRecomp
- N64: Recompiled: https://softwareengineeringdaily.com/2024/10/02/n64-recompiled-with-dario-and-wiseguy/
- RPCS3, high-resolution rendering: https://rpcs3.net/blog/?p=1015
- RPCS3, the surface cache: https://rpcs3.net/blog/?p=1701
- FSR 3.1 (open source, Vulkan): https://gpuopen.com/learn/amd_fsr_3_1_release/
- Streamline, DLSS-G on Vulkan: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md
- Streamline, the Linux problem: https://github.com/NVIDIA-RTX/Streamline/issues/127
- XeSS 3 SDK, Windows only: https://www.phoronix.com/news/Intel-XeSS-3-SDK-GitHub
- low_latency_layer (Reflex on AMD/Intel): https://www.phoronix.com/news/Low-Latency-Layer
- Killzone HD, developer interview: https://blog.playstation.com/2012/09/27/killzone-hd-re-developer-interview-part-1/
- Guerrilla, the SPUs in Killzone 2: https://www.guerrilla-games.com/read/the-playstation-3s-spus-in-the-real-world-a-killzone-2-case-study
