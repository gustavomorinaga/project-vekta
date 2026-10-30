# Moving to ps3recomp v0.18.0

The record of the move decided on 2026-10-27 and finished on 2026-10-28 (the plan is in
[upstream-and-reference-ports.md](upstream-and-reference-ports.md)). Patch numbers are the private series of the time
([patch-numbers.md](patch-numbers.md)).

## What was done

- The 107 patches on v0.14.0 were re-applied one by one: 72 went in (some adapted to the new code) and 35 were dropped
  because upstream already does the same. The series on v0.18.0 then had 74 patches, the last two new.
- **Vulkan backend:** ours stayed (two frames in flight, window modes, internal scale, crop and stretch, dumps), adapted
  to the draw engine's new interface. Upstream Vulkan improvements it does not have yet: per-target mask and blend in
  MRT (#516), depth bounds, 3D textures, sampler border colour, clears with a rectangle. (`depth_as_color` was done on
  top of our GPU depth packing.)
- **SPU fast float:** upstream's `SPU_FLOAT_RZ` only works on arm64. Our x86 mode (MXCSR in round-to-zero) came back,
  turned on by `g_spu_float_mode` in `stubs.cpp`.
- **Game memory:** v0.18.0 separates user memory (from `0x10000000`) from mappings (#296). The game heap moved from
  `0x40xxxxxx` to `0x11xxxxxx`. `native/jm.c` no longer uses fixed addresses: it gets the `jm_post` header and the
  "HiPrio" instance through `g_spurs_workload_added_hook`.
- **Lift:** `inline_hot_stores.py` no longer runs: v0.18.0 already puts every guest store inline.
- **Build:** with PGO. It builds and runs; Level 7 draws right.

## Speed

Level 7 (jungle, a saved game, 144 Hz clock), alternating A/B benchmark: v0.14.0 base **about 130 fps**; v0.18.0 with
PGO **about 131 fps**. The way there:

| Change | fps |
|---|---|
| First build | about 15 |
| `jm_post` off the SPURS workers without hanging (the kicks are still consumed) and the default poll period (the old kernel's 50 us got in the way) | about 28 to 38 |
| A reserved line's mark drops when a store ends every reservation on it (upstream never cleared it: walking the notifier was half the main thread) | about 51 |
| `depth_as_color` in our Vulkan backend (depth packed on the GPU, no readback) | about 82 |
| PPU memory accesses always inline | about 95 |
| Stores through the stack (r1) without checking the reservation bitmap | about 116 |
| `PPU_STORE_SNAP`: diagnostics and bitmap taken once per function; plain accesses on x86 | about 124 |
| SPURS: the coordinator sleeps on its own variable (the main thread's kicks woke the 4 workers) | about 127 |
| `PPU_STORE_SNAP`: each function's memory snapshot computed once and copied | about 131 (base: about 136 in the same session) |
| SPURS: ready counts use only the line lock (they do not wait for the coordinator's pass) | **about 131** (base: 131 to 135 alternating) |

The 1 to 2 % left (within the ±3 fps noise between sessions): the translated code still spends about 4 % more per
frame, the main thread is about 2 % idle (against about 0 on the base), and there are out-of-line loads
(`vm_read32`), coherence notifications and libc calls from the SPURS API.

## Left over

1. Check that `PPU_STORE_SNAP`'s plain accesses break none of the game's wait loops (run the 11 levels).
2. Port environment variables: `GCM_PARK_GRACE_MS=250` (it was our 0007's default; upstream's is 50).
3. A/B of what upstream brought in place of ours: inline stores everywhere (ours gave −3 %), `hle_fast` and the line lock
   with a mutex (#577, #443), vertex fetch (#458 against our 0095).
4. Fixed debug addresses that still assume `0x40000000` (`main.cpp`, `KZ_JM_DUMPIMG`, `KZ_LFQ_WATCH`).
5. Windows: `pad_active_device()` exists only in the SDL2 backend.
6. ~~11 levels~~: all 11 load and run 2 minutes without a hang (2026-10-27).

## First-person weapon parts jumping (144 Hz mode): reduced, on hold since 2026-10-28

Reported on 2026-10-27, with a video. In busy areas (the concrete yard with the Helghast dropships), parts of the
first-person weapon (body, sight housing, rail) appear each frame in a different pose from the others. Decision of
2026-10-28: left as is for now. To play without it, use the 60 Hz mode or `KZ_JM_SKIP_US=10`.

**What is known:**
- It is a v0.18 regression: the v0.14 build is normal in the same place.
- It happens only with the game clock at 144 Hz (`KZ_TICK_HZ=144`). At 60 Hz it is stable. The vblank the game sees
  (`KZ_GUEST_VBLANK_HZ`) does not matter.
- An F12 capture in the yard: after the depth clear the weapon's draws are the same every frame, but the mesh of some
  parts is identical to the previous frame's while the others move.
- The vertices are written into the RSX's local memory (`0x00B8xxxx` to `0x00C2xxxx`, a pool reused every frame) by
  `jm_gfx` (wid 2, HiPrio instance), with SPU DMA. The timeline of SPU PUTs and RSX reads shows the job runs and writes
  before the read, but with the previous frame's result: it read the bone matrices before the PPU updated them. The
  matrices come from the PPU (heap `0x12…` to `0x1A…`), not from another job.
- `jm_gfx` is kicked by the game (ready count, about 80 % of the time) and by the SPURS scheduler's polls (about 20 %).
  With the game's kick only (`PS3_SPURS_KICK_ONLY`, an experiment) the game hangs, so the game depends on the polls.

**What fixes it (tested by the maintainer in the yard):**
- `KZ_JM_SKIP=0 PS3_SPURS_WID_LANES=1:0` (`jm_post` computed by the native threads): stable, but Level 7 falls from
  about 133 to about 62 fps.
- `KZ_JM_SKIP_US=10` (skipped jobs take 10 µs each): stable, Level 7 about 112 fps.
- The clock at 60 Hz.

So what matters is `jm_post`'s timing, not its result: when it ends instantly the PPU moves on and the skinning takes
the old matrices.

**What does not fix it:** invariant position in the vertex shaders (kept; it is right for LEQUAL passes);
`PS3_SPURS_INSTANCE_SPUS=HiPrio:3`; `GCM_PARK_GRACE_MS=250`; `SPU_COH_KEEP_MARKS=1`; waiting for `jm_gfx` to go idle
before ending each skipped job; limiting to 1 tick per frame (worse).

**Reproducing it:** a saved game near the yard with the CPU overloaded (20 to 32 busy loops) gives 5 to 20 % of frame
pairs with frozen parts at 144 Hz and about 0 at 60 Hz. Tools: `PS3RECOMP_CAPTURE_TRIGGER=<file>` (F12 by script),
`draws.txt` with each attribute's address, `PAD_STICK_FILE`, `PS3RECOMP_VK_DUMP_TRIGGER`/`_RECT`/`_BURST`. Note: under
overload `KZ_JM_SKIP_US=10` does not fix it. The local reproduction may not be exactly the same case.

**First try with Project Helghast's rule (2026-10-28):** `cellSpursRequestIdleSpu` now writes the number of idle SPUs
asked for (it was a stub; Killzone HD asks for 7 for each job manager), and `PS3_SPURS_WANTED=1` (an experiment) only
admits a workload that was kicked or asked for idle SPUs, without the polls. Result: the game runs, but Level 7 falls
from about 128 to about 119 fps, and under CPU load the frames with a frozen weapon were 6 of 77 pairs, against 5 of 39
without the rule: it cannot be said to fix it. The real kernel's full rule needs a ready count (the idle-SPU request
only adds to it), and with that our game hangs, as Killzone 3 did before its patches 1058 and 1077 (the policy module
answers "switch" when it zeroes its own ready count, and the PM wrote `FFFF` over the list through a wrong argument
layout). Those two are the next step.

**The fix (2026-10-28):** the `selectWorkload` interception (the poll a policy module makes to the kernel between jobs)
always answered 0. For workload 0's job manager that meant "continue"; for every other one, "switch workload". So
`jm_gfx` (wid 2) went back to the kernel after each job and depended on another scheduler poll to take the next one,
which delayed the skinning jobs irregularly. Now the answer is the current workload (`wklCurrentId`, LS `0x1DC`), as
the real kernel does, after Project Helghast's patch 1058. The port turns it on by default (`PS3_SPURS_PM_SELECT=1` in
`stubs.cpp`). Under CPU load: 0 of 128 frame pairs with frozen parts (before, 6 of 41). Level 7: 127 to 128 fps, the
same as before. With this fix, the kernel's full rule (`PS3_SPURS_WANTED=2`: only what the game kicks runs, no polls)
no longer hangs either: 0 of 77, 129 fps. It stays an option, not the default. `KZ_JM_SKIP_US` is no longer needed for
this case.

**What is left (2026-10-28, on hold by the maintainer's decision):** in the dropship yard, strafing, there are still
frames where **the whole weapon** (the 13 draws after the depth clear) is identical to the previous frame's while the
world moves. The RSX's motion blur mixes in the previous frame, and the weapon "ghosts". The pool addresses change
between frames, but the data is the previous frame's. At the saved game's point this case is rare: 1 or 2 in about 50
to 130 frame pairs, under load or strafing.
- Does not fix it: `PS3_SPURS_WANTED=2` (the kernel's full rule).
- Improves it, according to the maintainer: `KZ_JM_SKIP_US=10` together with the fix above. Cost in Level 7: 10 µs
  about 111 fps; 3 and 5 µs cost nothing (127 fps, the same as 0). Still a manual option. Left to test: 3 and 5 µs in
  the yard; if 5 fixes it, it can be on by default for free.
- `KZ_JM_GFX_SYNC_US` (an experiment, off): holds the last job of each `jm_post` list until `jm_gfx` is idle. In our
  runs it never waited: `jm_gfx` was always idle at that point. What the 10 µs do is give the whole frame slack, not
  cover a visible sync point.
- Next step, if picked up again: a checkpoint near the yard (to reproduce it), and in the binary, the order on the PPU
  between the wait for `jm_post`, the write of the weapon's bone matrices and the kick of `jm_gfx`.
