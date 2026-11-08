# Moving to ps3recomp v0.19.0

Done 2026-11-08 on branch `ps3recomp-v0.19`. The submodule moved from v0.18.0 (`4e8f3ca8`) to v0.19.0 (`ae92407`,
81 pull requests, 2026-11-06), and the series was re-applied one patch at a time with `git rebase` on the new
release (about 25 conflicts, each resolved by hand). The earlier move is in
[ps3recomp-v018-migration.md](ps3recomp-v018-migration.md).

## What changed in the series

- **Dropped: 1006** (one-load SSSE3 local store access). v0.19.0's `SPU_LS_VEC` (#637) is the same load and
  `pshufb`, on by default on x86-64 with SSSE3; ours was dead code under it.
- **Smaller: 1017** (`cellSpursRequestIdleSpu`). v0.19.0 implements it (#649) with argument checks; the patch now
  only makes its store go through the lock line, as the SPU-side kernel reads it.
- **Kept: 2005, 4002, 1014.** Upstream's handler thread (#647, `GCM_HANDLER_THREAD`, off by default) and idle cap
  (#659, `SPURS_IDLE_MAX`) sit beside ours; upstream's keyboard fallback (#612) maps fewer keys than 4002 (no sticks
  on W/A/S/D and I/J/K/L) and is built only without SDL (patch 0007).
- **New 0006:** the Vulkan backend stays the port's. v0.19.0 changed `rsx_vulkan_backend.c` in #614 (resizable
  window), #681 (R32F and RGBA32F targets), #682 (stale releases off live objects) and the depth clamp; the port's
  patches rewrote the same code, so 0006 puts the v0.18.0 file back first and every later patch applies unchanged.
  The four are to be brought in one by one, each checked in the game.
- **New 0007:** one keyboard fallback, one `AUDIO_WAV` writer (both existed twice), and patch 0002's mark drop
  walking the reservation holders as the array v0.19.0 made of them (#672).
- **New 0008:** four of v0.19.0's new host tests link what the port's patches call (as 0004 does).
- 58 patch files changed only in their context lines and offsets.
- The lifters changed in v0.19.0 (`ppu_lifter.py`, `spu_lifter.py`): a new lift is needed (`tools/lift.sh`, about
  40 s here).

## How it was checked (2026-11-08)

- `tools/ps3recomp-patched.sh`: 131 patches apply. ps3recomp's own tests on the patched tree: 137/137.
- Linux build and Windows build (llvm-mingw) compile; the Windows build under Wine (`tools/wine-test.sh`, 90 s,
  headless): PASS, 144 fps, `queued` 2.
- Levels 1 to 8, the recorded routes replayed loose (`KZ_ROUTE_LOOSE=1`, 144 Hz clock, headless, no PGO in either
  build), one run at a time, the v0.18.0 build of `main` then this one on the same route:

| Level | v0.18.0 (fps) | v0.19.0 (fps) |
|---|---|---|
| 1 | 120.8, 130.5, 131.5, 134.7, 135.4 (mean 130.6) | 113.5, 123.4, 132.2, 139.9, 139.3 (mean 129.7) |
| 2 | 146.5 | 149.4 |
| 3 | 187.2 | 212.7 |
| 4 | 228.3 | 243.5 |
| 5 | 140.4 | 145.4 |
| 6 | 142.0 | 138.4 |
| 7 | 109.8 | 114.2 |
| 8 | 96.2 | 108.5 |

  Every route ran to its end on both, with the same frame counts and the same position differences (level 4 exact,
  0 differences, on both). Level 1 looked about 6% slower in the first two pairs; three more (two with this build first) put the means within 1% (130.6 against 129.7). In every pair the second run was the slower one, whichever build it was: the order, not the build.
- Not checked: the 144 Hz weapon-part bug (needs a frame capture), levels 9 to 11 (no recording), a PGO build, real
  Windows and an AMD card.
