# jm_post (the WWS post-processing job manager): reverse-engineering notes

Goal: take Killzone HD's post-processing off the emulated SPUs (about 10 ms per frame on 3 lanes) and run it as native
code on several cores.

Patch numbers in this note are the private series of their date ([patch-numbers.md](patch-numbers.md)). Addresses
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

Later results (the NV0039 copies, the MLAA on the GPU) are on the development branch and will come with its merge.
