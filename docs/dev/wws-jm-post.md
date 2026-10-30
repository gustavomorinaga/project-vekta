# jm_post (the WWS post-processing job manager): reverse-engineering notes

Goal: take Killzone HD's post-processing off the emulated SPUs (about 10 ms per frame on 3 lanes) and run it as native
code on several cores.

Patch numbers in this note are the private series of their date until the section "Which module is which" ([patch-numbers.md](patch-numbers.md)). Addresses
`0x40xxxxxx` are from ps3recomp v0.14.0; on v0.18.0 the game heap moved to `0x11xxxxxx`.

## The chain, per frame (Level 1, about 625 jobs)

Buffers in main memory (RGBA8):
- `0x404D2000` 1280x720 (3600 KB): the full image.
- `0x40856000`, `0x40937000`, `0x40A18000`: 640x360 (900 KB, pitch 0xA00).
- `0x40E7D000`, `0x40EB5400`: 320x180 (225 KB).
- Other inputs: `0x40310000` (900 KB), `0x4043C500`, `0x403F1000`.

Order (modules by overlay id; 200=`14e0`, 201=`6b71`, 202=`8197`, 203=`85fc`, 204=`ab05`, 206=`af58`, 207=`de0b`,
209=`f601`, 210=`f97b`):
1. `8197` x41: inputs `0x40310000`/`0x4043C500` -> `0x4095CF80..0x40AF9000`.
2. `85fc` x2, `af58` x5, `85fc` x2, `af58` x5: on the full image, in place (probably MLAA: edge detection plus blend,
   horizontal and vertical).
3. Half-resolution blur: `ab05` (horizontal) x24 / `6b71` (vertical) x24, twice, between `0x40937000` and `0x40856000`.
4. `de0b` x12: reduces `0x40A18000` to 320x180 (`0x40E7D000`).
5. About 11 pairs of `ab05`/`6b71` x6 at 320x180 (`0x40E7D000` <-> `0x40EB5400`), one `8197` x6 in between.
6. `f601` x108: composes the full image + `0x40A18000` + `0x40E7D000` -> the full image and `0x40A18000` (bloom).
7. `de0b` x48: full image -> `0x40856000` (half resolution).
8. `14e0` x64: motion blur at half resolution -> `0x40937000`.
9. `f601` x96: blends `0x40937000` into the full image.
10. `8197` x8 at the end.

## Command list

The workload header at `0x40149D80` (256 bytes; the policy module reads all of it):
- `+0x30`: pointer to the list (`0x40149E00`, right after the header; inline commands from `+0x80`).
- `+0x40 + 16*spuNum`: one 16-byte line per SPU. `+0x70`: u16 `0x0297`, u16 ticket (`+0x72`, the next job), and a u16
  per SPU at `+0x74 + 2*spuNum` (the job that SPU took, `0xFFFF` free). Only 3 SPUs fit: SPU 3's line falls on `+0x70`.
- 8-byte commands: `00SS0001:EA` = a job with a descriptor of `0xSS` bytes at EA; `00000002:EA` = a barrier with a sync
  object at EA (`0x41999C00 + 0x30*n`); the u16 at the object's `+0x30` counts pending jobs (each job decrements it when
  it ends).

## Job descriptor (0x70 to 0xA0 bytes)

`cmd:arg` pairs: `0x0200/0x0220/0x0240` reserve buffer sets; `0xNNNNC440:EA` loads through a DMA list (NN words);
`0x02E0`; `0xNNNNA400:EA` loads the code (NN*4 bytes); `0x0420`; `0x0004E4E0:EA` loads 16 bytes of parameters;
`0x0800:EA` the barrier object to decrement at the end; `0x0600:flags` runs; `0x0A00` end. Then inline parameters,
for example `ab05`: `40856000 00A0003C 0A002035 ...` = output EA, 160x60 pixels, pitch 0xA00.

## Why skipping a job does not work

The job runs with interrupts on: during the job, the policy module serves DMA-completion interrupts (vector `0xA2C`,
routines `0x22D8`, `0x2CC0`, `0x2D08`...) and already loads the next job and writes the previous one's outputs.
Skipping the job's code (returning at once) hangs the level. Replacing the jobs means emulating the whole `jm_post`
job manager, not only each job's computation.

## More than 3 lanes

Confirmed: `jm_post` hangs with 4 or 6 lanes even when the extra lanes are only turned on in the middle of gameplay
(`KZ_EXP_LANES_AFTER`, an experiment). It is not the level load: the game's job manager does not run with more than
3 SPUs. The 16-byte lines at `+0x40..+0x7F` of the header all have the same format (u16, u16 ticket, 6 u16 per SPU),
and at level load their content is different (`1FFFFFFF`), so the "one line per SPU" reading of patch 0058 is
incomplete; the limit of 3 holds anyway.

## End-of-list protocol (PPU side, `func_003A952C`)

The header has 4 queues (`slot` 0 to 3). For queue `s`: the list pointer at `+0x30 + 4*s`; the state line at
`+0x40 + 16*s` = u16 base, u16 ticket (the next command to take) and 6 u16, one per SPU (the command in progress,
`0xFFFF` free). The PPU computes target = base + the number of list commands from the base up to a command whose u16
at `+2` is 0, and considers the list done when the minimum of the ticket and the 6 slots is >= target. `jm_post` uses
queue 3 (`+0x70`, list at `0x40149E00`).

## Job allocation (policy module, `0x23C8`)

Under GETLLAR/PUTLLC on the header's first line: queue `q` = the u32 at `+0x08` (3 in the game), line `+0x40 + 16*q`,
list at `+0x30 + 4*q`. The command at the ticket is read from the list: type (low u16) 0 = end (nothing to take;
invalidates the cached list line at LS `0x14C0`), 1 = job (ticket + 1, the SPU's slot = ticket), 2 = barrier (passes,
ticket + 1, only if the u16 at the object's EA is 0; otherwise returns without a job). On every advance, u16 `+0` =
max(u16 `+0`, ticket + 1). At the end of a job: decrements the barrier object's u16 (the descriptor's `0x0800`
command) and the slot goes back to `0xFFFF`.

Descriptor commands: the low byte of each `cmd16` gives the buffer set (`(cmd16 >> 5) & 0xF`) and the operation
(`cmd16 >> 9`): `0x0200/0x0220/...` reserve the set (arg `PPNNBB00`: first 1 KB page, pages, buffers), `0xC440/0xC460`
load the DMA list into set 2/3, `0xA400` the code into set 0 (LS `0x4000`), `0xE4E0` 16 bytes into set 7. The module
enters at `0x4030` with r3 = the job context at LS `0x1330` (`+0x20` the parameter pointer; `+0x30..+0x50` kernel
functions: `+0x34` = `0x2F38` "use buffer", `+0x44` = `0x17A0` "load the next job"), and writes its output with its
own DMA list (PUTL).

Cost per job on the lanes (Level 1, per frame): `14e0` 124 us x64 (7.9 ms), `f601` 29 us x203 (6 ms), `af58` 501 us
x10 (5 ms), `ab05`/`6b71` 40 us x228 (9 ms), `85fc` 990 us x4 (4 ms), `8197` 60 us x54 (3.2 ms), `de0b` 34 us x60
(2 ms). `85fc`/`af58` (MLAA) are a few long jobs in series: the chain's critical path.

## More than 3 lanes (again)

The allocator accepts 6 slots, but with 4 to 6 lanes the level load still hangs: the policy module writes 16-byte
lines of its own at `+0x40 + 16*spuNum` (seen alternating `FFFFFFFF` and `00000000 1FFFFFFF...` on lines 0 to 2), and
with spuNum 3 and 4 they fall on queue 3's line (`+0x70`) and on the inline list (`+0x80`).

## The PC executor (`port/kz1hd/native/jm.c`)

PC threads take jobs as extra SPUs by the allocator's own rules, under the lock-line lock, using slots 3 to 5 (several
threads per slot: the slot holds the oldest ticket in progress). A job with a native version: computed on the PC,
output copied, barrier decremented. Any other job: runs in the game's own policy module in a private context (virtual
workload 12+, spuNum 0 to 2), with a private header at `0x3F000000` whose list holds only that job; the job does
everything it would on a lane (outputs, barrier) and the private header only takes ticket and slot.

With 3 threads: Level 1 uncapped 44.4 -> 48.4 fps (mean of the opening section), 52-53 -> 55-57 in the next section.
6 threads are worse (46.7). The native blurs match the SPUs byte for byte (64 million rows checked; the bottom edge
repeats the last row).

## The chain runs on empty buffers (2026-10-16)

The `jm_post` buffers dumped in the middle of gameplay (`KZ_JM_DUMPIMG`) are zero. The chain reads two 640x360 targets
the RSX draws into main memory (IO offsets `0x00010000` and `0x000F1000`, EA `0x40856000` and `0x40937000`: the first
from the scene at `0x0DE40000` and from depth), and the port's renderer keeps them on the GPU only. No draw of the frame
samples a texture in main memory or at 640x360: the chain's result does not reach the screen. Copying these targets
to memory after each draw (a test) makes the data arrive, but the colour channels are empty (alpha only), and
`0x0DE40000` was drawn only once, at the start.

So the default is now `KZ_JM_SKIP`: the lanes do not run `jm_post` (`PS3_SPURS_WID_LANES=1:0`, patch `0066`) and a PC
thread takes each job and completes it (ticket, slot, barrier) without computing. Level 1 uncapped about 60 -> about
64 fps, same picture. `KZ_JM_SKIP=0` goes back to the earlier path (3 lanes + 3 threads).

Level 1's gameplay in the reference footage looks the same as ours; the likely place where the effects are missing is
the depth-of-field blur of the cutscenes.

RSX IO: offset 0 = EA `0x40300000` (`cellGcmInit`, 21 MB). In 120 s (menus, movie, Level 1's start and gameplay), the
only main-memory textures the RSX samples are the two input targets (IO `0x10000` and `0xF1000`, once); no output of
the chain (`0x404D2000` = IO `0x1D2000`, `0x40856000`, `0x40937000`, `0x40A18000`, `0x40E7D000`) is sampled, and
`jm_post` does not write to the RSX's local memory (only `jm_gfx`, wid 2, writes there). The chain's result is not used
by what the game draws in that section.

## What the chain is (2026-10-29, from the names in the EBOOT)

The `jm_post` jobs have names in the EBOOT: `Gfx.Post.MLAA` (`PostMLAASPU`), `Gfx.Post.Transpose`,
`Gfx.Post.Downsample`, `Gfx.Post.VBlur`, `Gfx.Post.HBlur`, `Gfx.Post.Upsample`, `Gfx.Post.MotionBlur`
(`PostMotionBlurSPU`), `Gfx.Post.Zero` and `Gfx.Post.Modulate`. They match the order above: MLAA on the full image
(`85fc`/`af58`), a blur pyramid at half and quarter resolution for the glow (`ab05`/`6b71`/`de0b`, "Glow texture",
`GlowCopy`), motion blur at half resolution (`14e0`) and the compositions (`f601`). Each level's `PostProcessPreset`
resource holds Colorize (Contrast, Brightness, MixFactor, shadow, midtone and highlight colours), Motion blur
(Strength) and Noise (Grain size, Texture).

Main-memory render targets in Level 7 (the engine's log, 2026-10-29): only the two 640x360 ones, IO `0x10000` and
`0xF1000` (the "seeded from guest memory" ones); the full image `0x1D2000` is not an RSX target. Hypothesis: the RSX
draws into these two targets, the chain reads and writes them (the blur goes back and forth between `0x40856000` and
`0x40937000`), and the RSX samples them afterwards as textures. The port keeps the targets on the GPU and samples the
GPU copy, so what the SPU writes to memory never reaches the screen (the same problem Project Pyrrhus's patch 6008
solves for Killzone 2: write the targets to memory before the chain and upload what the SPU wrote before the next use).
The empty colour channels of the 2026-10-16 attempt are still to check.

Checked on 2026-10-29 (patch 0093, `RSX_ENGINE_SAMPLE_LOG`, `RSX_ENGINE_MAIN_TEX_LOG`, `KZ_JM_LOG`; IO base = EA
`0x11300000` on v0.18): in Level 7 the RSX samples only its two 640x360 targets (IO `0x10000` and `0xF1000`, about 150
times per second each). The chain reads these targets (the first job's DMA list starts at `0x113F1000` = IO `0xF1000`)
and writes other buffers (IO `0x1D2000`, `0x556000`, `0x637000`, `0x718000`, `0xB7D000`...), which no texture
samples, no NV3089 transfer copies and no target uses. With `PS3RECOMP_ENG_MAIN_COHERENCE=1` the targets go to memory
at every label release (the chain then reads the real image: about 227 thousand non-zero pixels), but nothing changes
on screen, because the chain's output is not read. Open question: where, on the PS3, the result enters the frame.
Hypotheses: only in certain scenes (cutscenes, the Luger, damage), which calls for sampling more sections; or by a path
the port does not reproduce yet (to check from whoever reads these EAs on the PPU).

More data (2026-10-29): modules `203` and `206` (MLAA) work on the full image `0x114D2000` (IO `0x1D2000`, 1280x360 per
job), which neither the RSX draws nor any transfer fills (`NV3089_DBG` shows only small font copies, and no
NV0039/NV3062); the display buffers are local (`0x0ED40000`/`0x0E980000`). The EBOOT registers the modules in
`FUN_000F3298` (ids `0x1A` to `0x27`), and static analysis does not find who queues them (access is through the TOC).
In the reference footage (chapter 027, about 3:05:00, Level 8's bamboo jungle) the picture is much greener and hazier
than ours at the start of Level 8, but the sections are not the same place: a comparison at the same point is needed
before concluding it is an effect of the chain.

Path: (1) make the chain correct, on the CPU (`KZ_JM_SKIP=0`) with target-memory coherence both ways, to get the game's
own reference picture, even if slow; (2) redo each effect as a GPU pass and compare with (1).

## Plan

1. Find where the game uses the chain's result on the PS3 (who writes `0x0DE40000`, which texture receives the output)
   and which effects show in the reference footage.
2. Reimplement those effects as GPU shaders on the renderer's own targets, checked against the footage; the modules
   already decoded (blurs, parts of `f601`) give the formulas.

## Which module is which (2026-10-29, from the images registered in `FUN_000F3298`)

Matching the `ila` constants of each lifted `jobmod_*` against the images the EBOOT registers:

| Overlay | Name in the EBOOT | Image in the EBOOT | Id |
|---|---|---|---|
| `af58` | `Gfx.Post.MLAA` (`PostMLAASPU`) | `0x5B2080`, 0x7090 bytes | 0x26 |
| `85fc` | `Gfx.Post.Transpose` | `0x5AEF00`, 0x3110 | 0x27 |
| `de0b` | `Gfx.Post.Downsample` | `0x5AD580`, 0x1910 | 0x1A |
| `6b71` | `Gfx.Post.VBlur` | `0x5AB500`, 0x2010 | 0x22 |
| `ab05` | `Gfx.Post.HBlur` | `0x5A8F00`, 0x2590 | 0x1C |
| `f601` | `Gfx.Post.Upsample` | `0x5A7200`, 0x1C90 | 0x21 |
| `14e0` | `Gfx.Post.MotionBlur` | `0x5A5080`, 0x2110 | 0x20 |
| `dfec` | `Gfx.Post.Zero` | `0x5A3A80`, 0x1590 | 0x25 |
| `8197` | `Gfx.Post.Modulate` | `0x5A0780`, 0x1890 | 0x1F |

MLAA on the full image is then: transpose (`85fc` x2), MLAA (`af58` x5), transpose (x2), MLAA (x5). The module works on
rows, and the transpose gives it the other direction.

From here on, patch numbers are this repository's: `2034` main-memory target coherence, `2035` NV0039 copies, `2036`
MLAA on the GPU, `2037` the MLAA pass's cold compile ([patch-numbers.md](patch-numbers.md)).

## Where the result enters the frame (2026-10-29, static analysis + runs)

The code builds the addresses with `lis` + `addic` (not `addi`), which is why the disassembler finds no references;
searching for that pair directly in the instructions:

- `FUN_000F3298` registers the SPU modules in a table at `0x130A0C0` (8 bytes per id: EA, size/16, ...), read by
  `FUN_00067358(out, id)`. The jobs are built by `FUN_0010CFC8`/`FUN_0010E0F8` (a descriptor from the id), called only
  from `0x10E360`, `0x10E578` and `FUN_0010E9F8`: the post jobs are built once, when the object is created.
- The post object is a singleton at `0x8285A0` (0x1B0 bytes, created by `FUN_00110604` -> `FUN_0010E9F8`). Fields:
  `+0x20` = `0x100` (MLAA on; nothing changes it later), `+0x188`/`+0x1A4` = the RSX's two 640x360 targets (getters
  `FUN_00110CE0`/`FUN_00110D18`, which wait for job group 1 to end), `+0x78` and `+0x94` = two full 1280x720 images in
  main memory (alternating every frame), `+0xCC`/`+0x104`/`+0x120` half resolution, `+0x13C`/`+0x158` quarter
  resolution, `+0x15C..+0x164` the list of ready jobs.
- `FUN_00110E74` (called by `FUN_0021E28C` and by the callback `0x111078`) queues every job of the frame's parity (bit 1
  or 2 in `job+0x18`), flips the parity (`+0x198`) and sets `+0x19C` = 1.
- `FUN_001109E8` (end of frame, called through `0x29C444` <- `0x21B7BC`), with `+0x20 & 0x100`: copies the current draw
  target into this frame's full image with an NV0039 copy (`FUN_002FE3EC` -> `FUN_0031BB58`: methods `0x2184` and
  `0x230C..0x2328`), and, if `+0x19C`, copies the previous frame's full image (already processed by the SPUs) back into
  the draw target. MLAA, glow and motion blur are composed by the SPUs into that full image (`f601`), so everything
  returns to the screen through that second copy, one frame late.

Checked in the port (Level 1, `GCM_SUBCH_GATE`): the copies arrive on subchannel 1, 864 in about 3 s, always 0x384000
bytes (1280x720x4) as one line: `0x0E980000` (local) -> IO `0x7F9000`, IO `0x1D2000` -> `0x0E980000`, `0x0ED40000` ->
IO `0x1D2000`, IO `0x7F9000` -> `0x0ED40000`. The local targets are the two display buffers. The port sent subchannel 1
to the 3D engine (Killzone uses NV4097 on it too) and did not implement NV0039: the copies were dropped, the full image
stayed empty and what the chain produces never came back. That was the missing link (and why patch 2034 did not change
the screen). NV0039's methods `0x30C..0x328` also landed on the 3D registers as alpha/blend (`ALPHA_REF` to
`STENCIL_TEST_ENABLE`).

Implemented in patch 2035 (`PS3RECOMP_NV0039=1`, off by default): the transfer is recognised by its shape on subchannel 1
(`0x184`/`0x188` with two `0xFEED000x`, then `0x30C..0x328` in a run of 8 and a lone `OFFSET_OUT`); the source is read
back from the GPU first (`rsx_draw_engine_guest_read`) and the destination uploaded after (`rsx_draw_engine_guest_wrote`),
with the geometry of the surface starting at that address (`rsx_draw_engine_surface_pitch`). With
`KZ_JM_SKIP=0 PS3RECOMP_ENG_MAIN_COHERENCE=1 PS3RECOMP_NV0039=1`, in Level 1 the copies happen and the screen shows the
chain's output: the edges of the weapon's sight are smoothed as in the reference footage (chapter 005, about 1:16),
where the port showed stair steps. Glow and motion blur cannot be judged on still frames. In one of four runs with
`KZ_JM_SKIP=0` the game stopped in the menu waiting for the job group (`FUN_00067ADC`); it did not repeat, cause not
investigated. The cost (Level 7, a saved game, 144 mode, 90 s of play; the port without it: 143.2 and 143.4 fps):

| Configuration | fps |
|---|---|
| everything on (`KZ_JM_SKIP=0`, coherence, NV0039) | 36.7 / 37.2 |
| only the chain on the SPUs (`KZ_JM_SKIP=0`) | 59.5 |
| chain + coherence of the 640x360 targets | 42.8 |
| coherence + NV0039, chain skipped | 92.9 |

The chain on the emulated SPUs alone takes the game to about 60 fps, and the GPU readbacks (the half-resolution targets
at every label release, the whole screen twice per frame) take the rest. A faithful reference, but not for play: the
playable effects have to be GPU passes on the renderer's own targets, with no round trip through memory.

In motion (Level 7, turning right with the right stick at full for 1.5 s, every frame dumped, with and without the
chain): no motion-blur trail shows, and the ratio of horizontal to vertical gradients is the same in both cases. The
game itself explains it:

- Killzone HD's motion blur does not come from camera movement. `FUN_001106C4` (called from `0x29BFC0` with the
  strength at `obj+0x7C` and a resource at `obj+0x78`) calls `FUN_0004DD18`, which sets the Cg parameters
  `motionBias`, `motionScale` (`post+0x18C` = 4.0), `motionFactor` (the strength) and `motionRadius`
  (`post+0x190`/`+0x194` = 0.05 and 1.0) and draws a full-screen quad into the 640x360 target IO `0xF1000`; without the
  resource it only clears the target. The shader (pipe 47 in the capture) builds a radial field around a centre,
  multiplied by the factor. In the middle of the turn this target is constant (127,127,0 = the zero vector): the factor
  is zero in normal play, and the effect is event-driven (explosion, damage, cutscene).
- The other 640x360 target (IO `0x10000`) gets the reduced scene and depth (pipe 48, textures `0x0DE40000` and the
  zeta): the glow's input.
- Comparing, in the same run, the raw frame sent to the SPUs with the processed one that comes back
  (`NV0039_DUMP=<dir>`, while `<dir>/go` exists): the difference is only at edges (mean 0.68 of 255, maximum 120-125),
  with no overall brightness change. In Level 7 in normal play the chain applies only MLAA; glow and motion blur are
  zeroed by their parameters.
- The HUD does not go through the chain: the copy happens before the pass that draws the HUD on top.

## The MLAA measured on the game's own module (2026-10-29)

Method: `NV0039_PATTERN=<P6 1280x720>` (patch 2035, with `NV0039_DUMP`) replaces the frame sent to the SPUs with a test
image (the alpha stays, and is always 255 in the game's frame), and the dump of the frame that comes back shows what
`af58` (with the `85fc` transposes) did to it. The patterns sit in 160x90 blocks (near-horizontal and near-vertical
staircases with a step every n pixels, contrast ramps, one channel only, shapes).

First results (Level 7):

- **A per-channel threshold, not luminance:** a grey staircase 100 against 100+d: d <= 16 changes nothing, d >= 20 is
  smoothed; red only (or green, or blue) +32 is smoothed like grey +32, and +16 in one channel is not. The edge is the
  largest difference between channels, with a threshold between 17 and 20.
- **The blend on a staircase of period L (black 0 and white 255):** in each step, the light-side pixel in the first half
  is `127 + 256(k+0.5)/L` and the dark-side pixel in the second half is `256(k+1)/L - 130` (k = position in the step,
  0..L-1), checked for L = 4, 8, 32 and 64. The two pixels next to the step come out at 142 and about 114-125, because
  the other direction blends them too.
- Staircases of up to 64 pixels per step are smoothed whole.

Second round (fine threshold, period-8 staircase, grey 100): grey +17 changes 39 pixels of the block, red +17 none, grey
+18 and red +18 122, +19 154; green +18 142 and blue +18 155. So it is not "largest channel > threshold": the rule
mixes the per-channel difference with another measure (below).

The detection core is at LS `0x78A0`/`0x7938` of `af58` (the lifted code in `work/lift/spu/jobmod_af58da8c8c70_e0030`;
only `0x4000-0xB090` is the module, the rest is the kernel). What reads from it:

- The `shufb` masks at LS `0xAA10..0xAA80` (image in the EBOOT at `0x5B2080 - 0x4000 + LS`): `0xAA30` zeroes each
  pixel's alpha byte; `0xAA50` puts a byte of another register in the alpha byte; `0xAA40` spreads the alpha byte to R,
  G and B.
- For each pixel, `sumb` adds R+G+B (alpha zeroed), multiplies by `0x0555` (`mpyhhu`/`mpyu`) and keeps byte 1 of the
  product, `(R+G+B)*1365 >> 16`, in place of the alpha: a coarse luminance from 0 to 15.
- Then it compares, byte by byte, the absolute differences between the pixel and its neighbours (`absdb`) with that
  measure spread out (`clgtb`), and the alpha byte's result decides the edge (`clgt` against `0x00FFFFFF`).

## The MLAA outside the game (2026-10-29)

`tools/mlaa_harness`: a program that runs the lifted code of `85fc` (Transpose) and `af58` (MLAA) outside the game, on
any image.

- **Capture** (`KZ_JM_CAPTURE=<dir>`, `jm.c`; the recipe in `capture.sh`, with `KZ_JM_HLE=0` so only the lanes run the
  chain): when `<dir>/go` appears, for 3 frames, every transfer of the `jm_post` SPUs goes to `dma.log`/`dma.bin` (EA,
  LSA, size and the bytes), and each entry of jobs `203`/`206` writes the 128 registers and the LS to `job<n>.bin`.
- **What the capture showed:** the jobs do their own DMA (the policy module only brings descriptor, code and
  parameters), so each job's entry state and a fake main memory are enough. The buffer is the full image with 768 rows
  (pitch `0x1400`; the 48 extra rows fall at the start of the next 640x360 buffer and do not affect the top 720). One
  frame's chain: `85fc` x2 (each transposes half of the 128x128 blocks in place: buffer(r, c) =
  image(x = 128*(c/128) + r%128, y = 128*(r/128) + c%128)), `af58` x5 (256 columns each, a column = 6 segments of 128
  pixels), `85fc` x2 (undoes it), `af58` x5 (144 rows each). Right after, `f601` (Upsample, the glow) writes over the
  full image: in the frame that returns to the game the MLAA comes mixed with the glow.
- **Harness** (`harness.c`): the runtime's SPU context with the functions the lifted code calls (`spu_rdch`/`spu_wrch`
  with DMA on the spot, indirect dispatch through the `85fc`/`af58` and policy module tables, `cellSpursModulePoll` at
  LS `0x9D0` answered as the runtime does). The only stall-and-notify list is the policy module's preload of the next job
  (`0x2874`, size-0 sentinel), which only touches the job manager's state; the list goes straight through. About 0.13 s
  per frame.
- **Checked** (`check_harness.py`): the 71 jobs of two captures (test pattern and normal play, Level 7) rerun alone make
  the same PUTs, byte for byte; the 4 whole chains rerun on the frame the game gave leave the buffer equal to the game's
  (0 different pixels in the 720 rows). With the injected test pattern, the harness's output equals, in RGB, the frame
  that came back through NV0039 (`NV0039_DUMP`); with real frames the return includes `f601` (about 3000 pixels with a
  difference of up to 26 in Level 7), so the reference on real frames is the buffer after the last `af58`, taken from
  the capture.

## The MLAA in C (2026-10-29)

`port/kz1hd/native/mlaa_ref.c` (test CLI: `tools/mlaa_harness/mlaa_ref_cli.c`). Rules taken from `af58`'s code (an
instruction trace with the lifter's `--trace` running in the harness):

- **Flags (78A0, the column pass):** bit 7 = some channel differs by >= 18 from the right neighbour; bit 6 = the same
  with the one below; bit 5/4 = R+G+B sum larger than the right/lower neighbour's; bits 0-3 = (R+G+B)*1365>>16. The last
  4 rows have a fixed luma of 2; row 719 has no bits 6/4. Column 1279's right neighbour is column 1, already blended
  (the transposed layout). The row pass reads only the alpha.
- **Runs:** pixels with the edge towards the next row, cut when the polarity changes. A 1-pixel run: both rows blend 1/8
  (`(224a+32b)>>8`, 9930). A run with a corner (crossing) at both ends: entry with heights (A060); otherwise, with more
  than 2 pixels: ends at the inner crossings, the first start/last end with a valid height, else the first/last of any
  (8630). Heights: num/den of the lumas around the corner, multiplier = length (or the rest of the run to its end, in
  8630), accepted in [0,1), else 0.5.
- **Shapes (84C0):** Z (start in one row, end in the other) and, only without Z, U; applied in order (all the Zs, then
  the Us) in 16.16 fixed point with the slopes `(2-2h)/L` from frest/fi, the left half up to `(x0+x1)>>1`, with the SPU's
  16-bit saturations and multiplies.
- **Jobs:** each job works on a copy of its rows plus the one before and the one after and writes only its own; the
  column pass recomputes the flags of row r0-1 (already blended) and, in the last job, processes the pair (1279, 1280)
  with columns 1, 2, 3 after 1279.
- The SPU's floating-point arithmetic (as the port runs it: IEEE with truncation) is emulated in integers and matches the
  runtime in 400,000 cases.

Checked against the harness (`tools/mlaa_harness/regress_mlaa.sh`, which runs `check_mlaa.py`): 0.04 s per frame; on
test images with 8 pixels of single-colour border, every pixel equal to the lifted code; on the game's captured frames,
the whole alpha equal and only the edge deviation below.

**Edge deviation (accepted, not reproduced):** building the run lists (`9B90`/`9100`) handles 4 rows at a time, in
lanes; when one row of the block runs out of its quads with edges before the others, `9100` keeps reading its list past
the end (into the next row's raw list), uses those values as LS addresses (they wrap and read other buffers of the job)
and writes the result into the row's quad 0: flags, luma and run offset of pixels 0-3 become garbage for the rest of the
pair. Ends that fall at `x = 1280` read quad 0 of the pool's next buffer. Reproducing that means emulating the whole
job's LS memory map, which does not fit in a shader; the C and the GPU follow the algorithm without the garbage. Effect
measured on 8 Level 7 frames (the GPU dump against the lifted code on the same input): 3116 of 530,000 changed pixels
differ (0.59 %), 87 % of them within 8 px of the screen edge (the rest are runs that start there), mean difference 6 and
maximum 60 per channel, alpha identical.

## The MLAA on the GPU (2026-10-30)

Patch 2036 (`libs/video/vulkan/rsx_vk_mlaa.comp`), switched by `KZ_MLAA` (`native/jm.c`): `gpu` (default) sets
`PS3RECOMP_MLAA_GPU=1`; `spu` goes back to the faithful path (`KZ_JM_SKIP=0`, target coherence,
`PS3RECOMP_NV0039=1`; also the only one with glow and motion blur, which are zero in normal play); `off` turns it off.

- **Where:** at the NV0039 copy of the display buffer to main memory (`FUN_001109E8`, before the HUD) the renderer runs
  the pass on the buffer itself and drops the copy back of the previous frame's result: the image shown is the frame's
  own, already smoothed, without the console's frame of delay.
- **Structure:** a staged compute shader (push constant): a copy of the target into a buffer; flags; for each of the 5
  column jobs, the flags the job recomputes (column r0-1 already blended; in the last, column 1279 against 1 and columns
  1-3 after it), the blend operations of each row pair (one workgroup per pair, flags of 4 rows in shared memory, run
  heads, A060/8630 entries, the 8630 scan with all threads and atomics, and one op per pixel), and the apply: one thread
  per stretch of consecutive pairs with ops (a pair without an op restarts from memory), in order; then the same on the
  rows (each job's first pair has its own ops: the job does not load the row above, so that pair's heights come out
  without it). The SPU's float arithmetic runs in 64-bit integers (`shaderInt64`), with the slope table for height 0.5.
- **Checked:** `mlaa_vk_test` (the same SPIR-V, without the game) against the C: 0 different pixels in 22 images
  (captured frames, 8 frames dumped from the game with `PS3RECOMP_MLAA_DUMP`, random tests); in the game, the GPU's
  dumped output equals the C on the same input.
- **Cost** (Level 7, a saved game, 144 mode, 90 s; RTX 3070 Ti): without MLAA 138.6 / 141.0 / 141.5 fps; MLAA on the
  GPU 141.8 / 141.2 / 135.1 / 139.4; the faithful SPU path 36.4. The pass takes about 1.5 ms of GPU in isolation
  (`mlaa_vk_test`), hidden behind the PPU limit.

### The GPU pass's first-frame stall (2026-10-30)

Two Level 7 benchmarks with the GPU MLAA came out slow (93 fps over 90 s, 34.8 fps over 40 s) while the others gave
about 138-142. In the slow runs `kz-stats` shows about 36 s without flips at the start of the level (the game clock
kept running), ending at the first `GPU MLAA ... ran`; afterwards about 138 fps. Cause: the NVIDIA driver compiled the
MLAA compute shader from scratch in 36.2 s (`mlaa_vk_test`: 36181 ms with `__GL_SHADER_DISK_CACHE=0`, 4 ms from the disk
cache), and the pipeline was created at the pass's first use, on the frame. The driver's cache misses whenever the
executable changes, so the first run after each rebuild stalled; both slow runs were such first runs. Not MLAA cost.

Fix (patch 2037): the shader keeps one call site per heavy float helper (the driver inlines each call; the height and
slope emulation had 4 and 8 copies), so a cold compile takes 2.0 s, with identical results (`regress_mlaa.sh`: GPU = C
on every pixel); and the pipeline is built on a thread of its own through the renderer's pipeline cache, the pass
skipped until it is ready (the log shows `not run ... still being built` once, then `ran`). Ten 60 s runs afterwards
(144 mode), the first on the freshly built executable and two with the driver cache disabled: GPU 139.7 / 140.0 / 139.6
(no cache) / 143.3 / 143.3 (no cache) / 143.3 fps, off 141.1 / 142.6 / 143.5, SPU path 43.7; the level load took about
4 s in every run.
