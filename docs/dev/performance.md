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

- **PSGL (Sony's OpenGL ES, game code at about 0x2F0000 to 0x370000): about 35 % of the frame.**
  - `glDrawElements` = `func_003239CC`, `glDrawArrays` = `func_003238E0`, `glBindBuffer` = `func_002FDFC4`.
  - State validation: `func_0030B990`, about 9 %. It uses dirty bits at context+0x1750; the most common
    combinations are 0x37DDE and 0x37C9A.
  - Draw emission: `func_00354874` → `func_00353E08`, about 7 %.
  - Cg parameters: the dispatchers `0x36678C`, `0x366898`, `0x366998` and `0x366B74`. The fragment constant patch is
    `func_00344764`.
  - Rewriting in C++ gives about 1 % per routine. Inside validation, about 35 % of its own time is memory loads
    (latency), which a rewrite would pay too.
- **The memory store path:** `vm_write32` takes about 9 % of self time, already on the shortest path with a call.
- **Indirect calls and HLE:** about 3 % and about 2.6 % (HLE fell with patch 0108; what is left is the lwmutex itself).
- **Animation:** `func_001879F0` (quaternions) is already native. `func_00055400` and its neighbours (0x54FFC to
  0x5B9B0) are the engine's object drawing.

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

## What is left

1. **Rewrite PSGL in C++, routine by routine,** checked command by command (the model is
   `native_ppu/psgl_vp.cpp`). In order of weight: validation (`func_0030B990`, per dirty-bit block), draw emission
   (`func_00353E08`), the Cg dispatchers, the fragment constant patch. Expected gain: +5 to 10 % in total.
2. **Replace all of PSGL with a native GL-to-Vulkan implementation (driver HLE).** The biggest possible gain (the
   about 35 % plus the decoding on the RSX thread), but a large project.
3. **The RSX thread (about 61 % today):** the texture hash (about 13 % of it) only falls with tracking writes to video
   memory. For now it does not limit.
