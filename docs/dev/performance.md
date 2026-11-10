# Killzone HD performance

The state of the performance work when it was paused (2026-10-21), so it can be picked up again without redoing
measurements or repeating ideas already ruled out.

Patch numbers in this note are the private series of that time, on ps3recomp v0.14.0; the patches were carried to
v0.18.0 and renumbered for this repository ([patch-numbers.md](patch-numbers.md)).

## Where we are

Picked up again on 2026-10-27 in Level 7's jungle section (the maintainer's save, about 1,000 draws per frame,
3440x1440): **about 137 fps** at 144 Hz.

Level 1, 144 Hz mode (`KZ_TICK_HZ=144 KZ_GUEST_VBLANK_HZ=144`): **about 131 to 134 fps** (about 70 on 2026-10-20 and
about 99 at the start of the native-rewrite stage). The default play setting stays at a steady 60 fps.

CPU use per thread, read from `/proc` during normal play: the game's main thread (PPU) is **95 % busy** and the
FIFO/RSX thread **61 %**. The limit is the game's translated PPU code.

A steady 144 fps needs about 10 % more on the main thread.

**2026-11-09/10, v0.4.0 (ps3recomp v0.19.0), no PGO**, rebased routes replayed headless and loose at 144 Hz
([perf-bench.md](perf-bench.md)): Level 7 about 127 to 133 fps, Level 1 about 137 to 144 fps (route timedemos, not the
play figures above); the main thread 94 to 96 % busy in every run, the RSX/FIFO thread (named "frame clock") 54 to
62 %, the busiest `spurs pm` thread 20 % (Level 7) and 45 % (Level 1). Session drift on a desktop in use reached 20 fps
over a day (Level 7 141.8 in the morning, about 119 at night, the same binary): only alternating pairs are comparable.

## What paid off

| Change | Where | Measured gain |
|---|---|---|
| Two frames in flight on the GPU: the frame's batch is no longer waited for (`RSX_VK_SYNC=1` restores the old way) | patch 0094, `rsx_vulkan_backend.c` | about 108 to 129 fps (alternating A/B) |
| Loads and stores of the translated code test one precomputed address range instead of 5 or 6 conditions | patches 0091 to 0093, `ppu_loader.cpp` and the lifter header | about 99 to 107 fps (separate sessions, less firm) |
| Flip cadence at 1000 Hz in play, a shorter `vm_write` | patches 0083 to 0085 | about 80 to 94 |
| PPU integer registers in local variables (`ppu_regcache.py`) plus PGO | patch 0086 | about 94 to 98 |
| AltiVec operations in SSE/AVX | patch 0089 | about 1 to 2 % |
| zlib inflate of FIOS blocks on the host instead of the SPU (`native_ppu/fios_inflate.cpp`; `KZ_FIOS_SPU=1` goes back to the SPU) | port | the main thread's read wait from 19 % to 6.5 %; about +3 fps |
| Vertex fetch per attribute, index dedupe without hashing | patch 0095 | about 1 % (alternating A/B) |
| Native functions: animation interpolation, vertex program upload and PSGL's `memcpy` | `native_ppu/` | each below the noise |
| Command hash in 8-byte steps, texture hash by ranges, direct FLOAT/UNORM8 vertex fetch (`RSX_HASH_BYTES=1`, `RSX_TEX_HASH_FULL=1`, `RSX_VTX_SLOW=1` restore the old ways) | patch 0107 | +2.3 %, +3.4 % and +2.5 % (alternating A/B, Level 7) |
| The PC menu's Lua script runs only when a short probe finds something pending | `pcoptions.cpp` | Lua on the main thread from 4.1 % to 0.6 % |
| A per-thread shortcut for table HLE calls (the allocator's `sys_lwmutex_lock/unlock`); `PS3_HLE_SLOW=1` restores the general path | patch 0108, `ppu_hle.cpp` | 126.5 to 129.2 fps (alternating A/B, Level 7) |
| The lock line shared with the SPUs waits by reading, with a pause, instead of repeated exchanges (`SPU_LOCKLINE_XCHG=1` restores the old way) | patch 0108, `spu_coherency.c` | 129.9 to 131.0 fps (alternating A/B, Level 7) |
| Inline memory stores only in the 80 functions that store the most (`tools/inline_hot_stores.py`, applied by the lift) | port | 131.5 to 136.8 fps (alternating A/B, Level 7); all 277 of the profile: the same (137.0 against 137.1) |

On ps3recomp v0.18.0 every guest store is inline already (upstream #573/#574), so `inline_hot_stores.py` no longer
runs.

## What was tried and dropped

Each item was measured in alternating A/B on the same executable, unless noted, and reverted:

- Caching floating-point and vector registers too in `ppu_regcache` (about 95 against 97.7 fps).
- Inline memory stores in all the translated code, even with only the range and bitmap test: −3 % (6 MB more code
  costs more than the call). Tried twice. Only in the hot functions it gives +4 % (above).
- `preserve_most` on the `vm_write*` functions: no gain.
- Inline loads as `volatile` without the alignment branch: slower.
- Conditional stores of `g_spu_coh_armed` (cache contention): no gain.
- A direct table for indirect calls instead of the hash: no gain.
- A longer flip queue (`PS3_GCM_MAX_QUEUED_FLIPS=2`): no gain, and one frame more of input lag.
- Checking flips at 8000 Hz instead of 1000 Hz: no gain. The first measurement gave +3.7 %, but it was drift between
  sessions.
- Publishing fence references faster (`GCM_REF_GAP_US`): no gain.
- `lwcond` waits polling every 1 ms: no gain (the waits are really signalled).
- Hashing static textures only every 8 frames: no gain in fps, and a visual risk.
- ThinLTO across the translated files: −2 %.
- PGO redone after the changes: no change (2026-10-21) and −0.8 % (2026-10-27, 136.1 against 137.2). The profile in
  use stayed the one of 2026-10-20.
- Large memory pages: the kernel already uses them (THP `always`; about 430 of about 600 MB of anonymous memory in
  2 MB pages).

## What costs on the main thread

Measured 2026-11-09 on v0.19.0 (`KZ_PROF_STACK=1`, 20 s of CPU-time samples in a route replay, Level 7 / Level 1;
inclusive unless said otherwise). The figures of 2026-10-21 (v0.14) are in brackets.

- **PSGL (Sony's OpenGL ES, game code at about 0x2F0000 to 0x370000): 26.4 / 27.8 % of the main thread** [about 35 %].
  - `glDrawElements` = `func_003239CC` 10.5 / 10.9 % (its own time 0.1 %), `glDrawArrays` = `func_003238E0` 3.1 /
    2.0 %, `glBindBuffer` = `func_002FDFC4` 0.4 %. The engine calls PSGL mostly from its object drawing,
    `func_00054FFC` (13 to 14 %).
  - State validation: `func_0030B990` 5.6 / 6.7 % [about 9 %], 1.6 / 2.1 % of its own. It uses 30 dirty bits at
    context+0x1750, one block per bit. Counted (`KZ_PSGL_BITS=1`, on a side branch): bit 3 is set in every validation
    and the mask is exactly 0x8 in 51 % (Level 7) and 41 % (Level 1) of them; 17 to 19 masks in all, then 0x37DDE,
    0x33DDE, 0x37C9A.
  - Draw emission: `func_00354874` → `func_00353E08` 7.2 / 5.4 % [about 7 %].
  - Cg parameters: the dispatchers `0x36678C` 1.6 %, `0x366898` 1.2 to 1.3 %, `0x366998` 1.0 to 1.2 % (it calls the
    fragment constant patch `func_00344764`, 0.7 to 0.9 %), `0x366B74` 0.05 %.
  - The function with the most time of its own inside PSGL is `func_0030EEDC` (3.2 / 2.2 %): not command output but
    PSGL's heap allocator (behind `func_003117DC` and `func_0031D6E8`), a free list by size class whose time is list
    walking; about 80 % of its calls fail and walk every list.
- **The rest is the engine's own code, spread thin:** about 88 % of the samples are in translated PPU code, and no
  function outside PSGL has more than 3.4 % of its own time.
- **The memory store path:** 1.3 to 1.5 % of the main thread [`vm_write32` about 9 %]: every store is inline since
  v0.18.
- **Indirect calls and HLE:** 3.3 to 3.5 % and 2.3 to 2.9 % [about 3 % and 2.6 %]; libc 1.6 to 2.1 %, the SPU lock
  line about 1 %, lwmutex 0.4 %.
- **The RSX/FIFO thread:** vertex fetch and index remap 25 to 31 %, FIFO decode and method dispatch 29 to 33 %,
  Vulkan recording and the driver 27 %, shader and state analysis and hashes 7 %, the texture hash 3 % [about 13 %].
- **Animation:** `func_001879F0` (quaternions) is already native. `func_00055400` and its neighbours (0x54FFC to
  0x5B9B0) are the engine's object drawing.

## Native PSGL, 2026-11-09/10

Two routines were rewritten in C++ from their observed behaviour and checked under the store journal
(`KZ_NATIVE_VERIFY=1`, rebased Level 1 and 7 routes): both at 0 mismatches, both without a measurable gain in
alternating A/B on one executable (`KZ_NATIVE_PSGL=0` against native). Kept on branches of their own, not merged.

| Routine | Checked (L7 / L1) | A/B, main thread CPU per frame B − A (95 % interval) |
|---|---|---|
| PSGL's heap allocator `func_0030EEDC` | 7,978,789 / 10,372,228 calls, 0 mismatched (2 / 14 not compared, another thread changed the heap) | Level 1 +0.15 % [−0.2 %, +0.5 %], 4 pairs; Level 7 inconclusive (a busy browser) |
| validation for mask 0x8 and its block 3 `func_0035D898` (only when block 3 fits in the room already in the command buffer) | 3,084,574 / 2,384,649 and 3,003,281 / 3,494,833 calls, 0 mismatched | Level 7 −0.81 % [−1.96 %, +0.35 %], Level 1 +0.89 % [−0.89 %, +2.66 %], 6 pairs |

A faithful C++ version of a lifted routine of 1 to 3 % does not save measurable time: the lifted code at -O2 is close
to it, and the time is mostly memory and calls. The self time in a profile does not predict the gain when it is
memory latency.

Two lessons for the check itself: the lifted body against itself first gave 165 differences in 7,977,716 calls, all
another PPU thread unlinking from the same heap during the check (now counted as "not compared" with an input probe);
and a native body that writes the command buffer must run only when its commands fit in the room already there, since
the buffer's callback hands commands to the RSX, which an undo cannot take back.

## Redundant work in the command stream, 2026-11-10

Measured with local instrumentation in the RSX decoder (not a patch; rebased routes, 14,336 frames). Per frame, Level 7
/ Level 1: 56,700 / 60,100 method words, of which **62.8 / 65.6 % set a value the RSX already holds**; vertex constants
20,600 / 20,100 words (66 / 77 % equal), vertex program words 14,400 / 15,300 (55 / 48 %), other methods 21,600 /
24,700 (64 / 67 %); about 750 draws; 52 to 53 % of the draws use a fragment program (constants included) byte for byte
the previous draw's.

Who emits them: the vertex constants come from validation's block 3 (`func_0035D898`), which copies a prepared block
of constant loads; the fragment program constants from the Cg dispatchers, as one-vec4 image uploads into the program
in video memory (a subchannel-3 surface and a subchannel-5 image from CPU, about 1,700 a frame); texture state from
`func_0035D4A4` (20 words a call); draw emission repeats method 0x1714 4.5 times a draw (always equal). Of the
fragment constant uploads, those of dispatcher `0x36678C` carry the same 16 bytes as the last upload to the same place
95.5 % of the time; those of `0x366898` 5.8 %.

Ceiling of skipping what is redundant (an estimate, not measured): dispatcher `0x36678C` up to about 1.5 % of the
main thread, block 3 up to about 0.6 %; about 2 % in all (Level 7: about 0.15 ms of 7.2 ms a frame). On the RSX
thread up to about a fifth of its time, but it does not limit.

## Tools and measuring with care

- **Compare only in alternating A/B on the same executable.** Use an environment switch, or two copies of the binary,
  both named `KZ1HD` in separate folders (`KZ_BIN=`); `pkill -x KZ1HD` does not kill copies with another name. Drift
  between sessions reaches ±3 fps and has already produced two false "gains".
- **Benchmarks at 144 Hz need `KZ_TICK_HZ=144 KZ_GUEST_VBLANK_HZ=144`.** `tools/pgo.sh` resets the test save, and with
  it the PC menu's setting. A result of about 61 fps means this was left out.
- **The first run after a rebuild sometimes does not reach the game** (pipeline caches). Do a warm-up run.
- **`KZ_PROF_WALL` distorts the result:** it delays the RSX thread and invents flip waits on the main thread (22 % that
  the trace showed to be about 0). To know which thread limits, read each thread's CPU use in `/proc` during a normal
  run. For waits, use `PS3_TRACE` with `tools/trace_frames.py`; there are `flip_wait`, `lwcond`, `usleep`,
  `rsx_fifo`, `gpu_wait` and `tex_hash` spans.
- **Profiles:**
  - `KZ_PROF=<start>,<seconds>,<file>` with `KZ_PROF_STACK=1`, then `tools/prof_report.py` with `--incl`, `--callers`
    or `--group`.
  - Per line: build with `-DCMAKE_CXX_FLAGS="-O1 -g1"` (the same generated code) and use
    `tools/prof_lines.py <prof> <bin> func_XXXXXXXX`, without `^`/`$`, because the name comes demangled.
- **Native PPU functions:**
  - They live in `port/kz1hd/native_ppu/`. `tools/lift.sh` passes their names to the lifter (`PPU_LIFT_NATIVE`), which
    emits the translated body as `<name>_lifted`.
  - `KZ_NATIVE_VERIFY=1` runs both versions and compares them.
  - `vm_block_plain()` says whether a block may be stored to directly.
  - Functions with wide effects (PSGL) are checked with the guest store journal (ps3recomp patch 3034): build with
    `-DKZ_STORE_JOURNAL=ON` in a folder of its own (`work/build-verify`; the option routes the lifted stores to the
    runtime while a journal is on, so it is for correctness runs only), and run with `KZ_NATIVE_VERIFY=1`.
    `native_ppu/verify.h` runs the native body with the journal on, undoes it, runs the lifted one with the journal on,
    and compares every byte either stored (above the entry stack pointer, without the caller's CR/LR save slots), r3
    and the registers a callee keeps; the lifted result stays. The counts (checked, mismatched, fallbacks) are printed
    at quit. `KZ_NATIVE_VERIFY_SELF=1` compares the lifted body with itself: the floor of the check (what the journal
    cannot undo, such as another thread's stores or state outside guest memory).
  - `KZ_NATIVE_PSGL=0` turns the native PSGL functions off (their lifted bodies run): the A side of an A/B on one
    executable (`tools/perf/ab.sh` with `A_ENV=KZ_NATIVE_PSGL=0`, [perf-bench.md](perf-bench.md)).

## What is left

1. ~~Rewrite PSGL in C++, routine by routine.~~ Two steps measured (above): no gain within the A/B's resolution
   (about ±1 %). Doing the rest of validation block by block would give gains of that size each.
2. **Skip redundant work in PSGL** (the commands that set what the RSX already has): about 2 % of the main thread at
   most, mainly Cg dispatcher `0x36678C`. It changes the command stream, so it needs a check of the RSX state at
   every draw instead of byte by byte, and invalidation against everything else that writes that state.
3. **Replace all of PSGL with a native GL-to-Vulkan implementation (driver HLE).** At most the 26 to 28 % that PSGL
   takes on the main thread, plus the decoding on the RSX thread, but a large project.
4. **The engine's own code** (about 72 % of the main thread, spread over thousands of functions): the next place to
   look for a steady 144 fps in Level 7, 5, 6 and 8.
5. **The RSX thread (54 to 62 %):** not the limit.
