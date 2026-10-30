# Port journal

The dated journal of the port, from the first lift (2026-10-14) onwards, translated from the maintainer's notes. Paths
are given relative to the work folders the scripts use now (`work/lift`, `work/fw`, `work/vfs`); commands of the time
are kept only where they still explain something, and today's route is in [../BUILDING.md](../BUILDING.md). Patch
numbers are those of each date's private series, on ps3recomp v0.14.0 until 2026-10-27; they are not the numbers in
`port/patches/` ([patch-numbers.md](patch-numbers.md)). Plan: [recomp-plan.md](recomp-plan.md).

## Reference machine

CachyOS, GCC 16.2.1, clang 23.1.1, Python 3.14, NVIDIA GeForce RTX 3070 Ti (driver 615.78.08, Vulkan 1.4.351).
Packages: `glslang`, `spirv-tools`, `vulkan-headers`, `vulkan-icd-loader`, `sdl2-compat`, `sdl3`, `wine`.

## ps3recomp (phase 0)

Pinned at tag `v0.14.0` (commit `a2c2010`) then; v0.18.0 since 2026-10-28. Built with
`-DPS3RECOMP_RSX_VULKAN=ON -DPS3RECOMP_BUILD_TESTS=ON`; CMake reports `Vulkan: guest programs available`.

Checked on 2026-10-14:
- `ps3recomp_host` with `PS3RECOMP_VK_GUEST_PROGRAMS=1` on the RTX 3070 Ti: the 13 scenes (`--draw --depth --quads
  --tex --tex-io --shader --mip --rtt --depthtex --mrt --mrt-a --cube --threads`) pass.
- `ctest` (sync, fibers, runtime): passes.
- `tools/check_ppu_scaffold.py`: no regression against the `linux-gcc` baseline.

Open at the time: `mingw-w64-gcc` for the Windows build, validation layers and RenderDoc for debugging; and Vulkan on
Windows: ps3recomp v0.14.0 refused `PS3RECOMP_RSX_VULKAN` outside Linux and loaded the loader with `dlopen`. (Done
later: [windows-build.md](windows-build.md).)

## Phase 1: Killzone HD's PPU translated (2026-10-14)

The port itself (CMake, `main.cpp`, `stubs.cpp`, from ps3recomp's `templates/project`) is in `port/kz1hd/`. The steps
of the time are what `tools/lift.sh` does now: `ppu_loader.py`, `find_functions.py` with the static analysis's function
starts as seeds (now `re/function_seeds.txt`), `ppu_lifter.py --code-end 0x52f740`, then CMake with clang `-O1`.

- `ppu_loader`: 17,608 functions from `.opd`, 269 imports, entry 0x373DB8, TOC 0x5EE4A0. Executable sections end at
  0x52F740 (`--code-end`).
- `find_functions` alone found 23,610 functions, but 20 of the 22,821 found by static analysis were missing. With those
  as seeds: 23,363, covering all of them.
- `ppu_lifter`: 23,529 functions (150 entries in the middle of functions, 42 jump tables with 265 cases) in 11 C++ files
  of about 150,000 lines. The only missing instruction was `vmsumuhm` (44 uses), implemented in the lifter. 1,893
  `.word` remain (data inside code) and 1 invalid opcode in a spurious function below 0x10200.
- It compiles and links with clang -O1 (a 36 MB executable).
- `vmsumuhm` ended up covered by PR #269 (the right big-endian lane order in ten VMX operations); our own patch was
  dropped.
- **First run:** the game sets up TLS and runs Guerrilla's own allocator ("PIGS malloc init"), but the
  `sys_memory_allocate` of 0xB800000 bytes returns no address, the heap stays at 0 and the next writes land on NULL. The
  first problem of phase 2.

## Phase 2: boot (2026-10-14)

### ps3recomp's `kz` branch

A local branch `kz`: `v0.14.0` plus the open PRs of the Killzone 3 port (by undecuple), which deal with the same engine:
#253, #254, #255, #256, #257, #258, #268, #269, #270, #271, #272, #273, #275, #276, #277, #278, #279, #280, #281, #282,
#285, #297. Conflicts: #269 (lifter, the PR's version kept) and #279 (a test only, ours kept). Host scenes and `ctest`
still pass.

Our own patch: the jump-table detector did not find the table's base when it was stored on the stack before the
`switch` loop (`lwz r7,d(r2); std r7,0x120(r1); ... ld r10,0x120(r1)`), and read the `add r10,r0,r10` after the
`lwzx` as the base's definition. With the fix: 43 dispatchers and 272 cases (before 42 and 265). A PR candidate.

### Fixes in the port

- `main.cpp`: on Linux the guest memory was a 3.5 GB `calloc`, which returns a pointer 16 bytes past a page start;
  every `mprotect` of `vm_commit` failed and the game heap's `sys_memory_allocate` gave ENOMEM. Replaced with `mmap`
  (worth taking to the upstream template too).

### The run folder

The game asks for `/dev_bdvd/PS3_GAME/USRDIR/data_ps3.psarc` (and falls back to `.edat` if it is missing). The runtime
resolves `/dev_bdvd` relative to the current folder, so the port runs from inside the run folder (today
`tools/setup-vfs.sh`):

```
vfs/PS3_GAME/PARAM.SFO                -> a copy of the installed game's PARAM.SFO
vfs/PS3_GAME/USRDIR/EBOOT.elf         -> a link to the decrypted EBOOT
vfs/PS3_GAME/USRDIR/data_ps3.psarc    -> a link to the decrypted archive
```

### How far the boot gets

TLS, Guerrilla's heap (184 MB at 0x40000000), sysutil, cellGame, two SPURS instances ("HiPrio", "LoPrio") with nine job
workloads (`jm_hiprio`, `jm_post`, `jm_gfx`, `jm_particles`, `jm_partcl_ray`, `jm_amadeus`, `jm_physics`, `jm_raycast`,
`jm_generic`) and two tasksets, FIOS opening the archive, cellGcm initialised, 1280x720 display buffers, 32
`DRAW_ARRAYS` and 3 flips. Then the main thread waits for an event flag only an SPU task sets: the SPU programs are not
translated yet ("PM NOT LIFTED"), so phase 3 starts here.

NIDs without an implementation so far: `0x94034C95` (`cellSpursTasksetUnsetExceptionEventHandler`), which returns 0
with no visible effect, and `0x8A85674D` (`_cellSpursLFQueuePushBody`), solved in phase 3.

## Phase 3: SPU, SPURS and movies (2026-10-14)

### SPU code

Killzone HD's EBOOT holds SPU code in three forms:

| Kind | How many | How it comes out |
|---|---|---|
| SPU ELFs | 3 (`spu_0000..0002`; 0001 is the audio task, 0002 the LFQueue consumer) | ps3recomp's `extract_spu_images.py` |
| The WWS job manager's policy module (raw, 12,160 bytes at 0x5B9180, used by the 9 `Wws_Job` workloads) | 1 | `tools/extract_spu_modules.py --pm 0x5B9180:12160` (wrapped as an ELF at LS 0xA00) |
| Job modules (`ila … 0xC0DEC0DE`, raw, loaded at LS 0x4000) | 11 | `tools/extract_spu_modules.py` |

The EBOOT's other 60 `\x7fELF` are RSX Cg shaders (`SCE cgnv2elf`), not SPU code. The translations have no missing
instructions; only `.word` (data in the middle of code) remain. The 9 workloads find the policy module by its
fingerprint (`0xFE229D83A0B8B05D`).

### The firmware's libsre (the LFQueue only)

The main thread waits for an event flag that SPU task 0 of the "LoPrio" taskset sets after receiving work through a
`CellSpursLFQueue`. The PPU side of that queue (`_cellSpursLFQueueInitialize` and `_cellSpursLFQueuePushBody`) does not
exist in the HLE, and the protocol lives in guest memory and SPU code. Instead of rewriting it, the port runs the real
code of `libsre.prx` from the player's own firmware, for those two functions only; the rest of SPURS stays HLE. The lift
is `lift_prx.py --base 0x02000000`, the imports turned into the lifter's `--hle-stubs` form, then `ppu_lifter.py --raw
--base 0x02000000 --toc 0x02039AB0 --code-end 0x0201DD4C` (now in `tools/lift.sh`).

At run time `KZ_LIBSRE_IMAGE` points at `work/fw/out/libsre_image.bin` (`tools/run.sh` and `tools/play.sh` set it). In
`stubs.cpp`: the image goes to 0x02000000, the 384 functions are registered, the two NIDs become calls into the real
code, and the `fpSendSignal` the push passes (`cellSpursSendSignal`, code 0x020127CC) is swapped for the HLE one, which
is what wakes the SPU task parked in `WAIT_SIGNAL`. The queue belongs to SPURS, not to a taskset, so the signal arrives
as `(spurs | 1, wid << 8 | task)`; a patch gives the HLE a `(spurs, wid) -> taskset` table to resolve it.

### Movies

Nothing decoded the AVIs yet (phase 6). To keep the game from hanging:

- `cellSailAviMovieGetStreamByIndex` and other SAIL functions had no handler; a NID without a handler returns 0, and the
  game's loop over the streams (`FUN_000614a8`) never ended. `stubs.cpp` registered minimal handlers: a movie with no
  streams and no frames.
- The movie's state machine (`FUN_00061B90`) only advances if it sees `RUNNING` before `OPENED`, but the HLE reported
  both inside `PlayerStart`. A patch allowed the movie's end to be deferred to the game's next SAIL call.

### How far the boot gets

The event flag is set, the game loads the save, opens audio (the SPU audio task runs), goes through the opening movie
(`Movies/Intro/Intro.avi`, skipped) and enters the menu: it opens the background movie `menu/frontendassets/campaign.avi`
on repeat. From there the main thread waits for an LV2 event flag (`func_00087270`) and nothing more is drawn (only the
loading screen's 32 `DRAW_ARRAYS`, 3 flips). The renderer in use was still the null one; turning Vulkan on and getting
the menu on screen was phase 4.

## Phase 4: Vulkan (2026-10-14)

- `port/kz1hd/main.cpp` uses ps3recomp's Vulkan backend whenever it is built (`PS3RECOMP_RSX_VULKAN`), before
  D3D12/Metal/null. The game's own shaders run with `PS3RECOMP_VK_GUEST_PROGRAMS=1` (HLSL -> SPIR-V through glslang).
- `KZ_STATS=1` prints every 2 s how many flips the game asked for.
- **First frame:** the title screen with the logo and the legal notice comes out right through Vulkan
  (`PS3RECOMP_VK_DUMP=<file.ppm> PS3RECOMP_VK_DUMP_EVERY=5000`).
- **Pacing:** Killzone HD does not wait for the flip (no `cellGcmSetWaitFlip`, no flip status); on the PS3 the RSX holds
  the game, passing a `VSYNC` flip only at the next vblank, and the game waits for room in the ring. The HLE skipped
  flips when the FIFO fell behind (good for VF5, bad here) and `main.cpp` drained the FIFO outside the tick: thousands
  of flips per second. A patch (`cellGcmHleSetStrictFlip`, on in `stubs.cpp`) now always stops at the flip, and
  `main.cpp` drains at most one flip per 16 ms tick: about 62 flips/s, steady.
- **Menu:** the game reaches the menu in about 48 s (logos and legal notice run on their own, without input). The menu
  is a single full-screen texture per frame, from the background movie `menu/frontendassets/campaign.avi`; without video
  decoding the frame stays black. The menu depends on phase 6 (movies through cellSail).
- `PS3_DEV_FLASH` lets the game open `flashMP3.pic` (MultiStream's MP3 codec).

## Phase 6 early: movies (2026-10-14)

The menu and the opening are AVI movies (MPEG-4 Part 2 + MP3, 1280x720; the opening has 5 audio tracks, one per
language). `port/kz1hd/sail.cpp` replaces the cellSail HLE in all 43 functions the game imports and plays the movies
with FFmpeg:

- The protocol read from the game's own code: the player's callbacks (`(arg, major<<32|minor, arg0, arg1)`, with the
  `CellSailFuture` in `arg1`), the movie's state machine (`FUN_00061B90`), the AVI database (`GetMovieInfo`
  +0x1C/+0x20/+0x24, `vids`/`auds` streams, `SetEs` for the language track).
- Frames go into buffers the game allocates through the graphics adapter's `alloc` callback (up to 1280x720x4 + 0x80),
  RGBA, delivered at the movie clock's pace; audio in blocks of 256 stereo float 48 kHz big-endian frames for the game's
  `cellAudioAdd2chData`.
- The 30 AVIs are taken out of the archive into the run folder (today `tools/setup-vfs.sh`).
- A patch: the RSX ignored a linear texture's pitch when it was smaller than its width; the game points a 2048x1024
  texture at the 1280x720 frame with pitch 5120, and the movie showed as five copies side by side.
- The movie-end deferral patch was reverted: `sail.cpp` covers it.

Result: the notices, the Sony and Guerrilla logos, the 3-minute opening (with sound) and the title screen show right
through Vulkan. `START` skips the opening (`PAD_SCRIPT` simulates the pad).

### Where it stopped

After the title the game loads the menu (`campaign.avi`) and hangs. Two causes found:

1. **FIFO:** the HLE walker treats a `JUMP` to itself as a "park" and, if `put` has already moved, jumps straight to
   `put`, dropping the commands in between. In Killzone HD that dropped the releases of label 0xFD the main thread waits
   for (`FUN_00361DE8`). A patch: the park gets 250 ms of grace and, with `GCM_PARK_SKIP=1` (the default in the run
   scripts), the walker skips only the park word and runs the rest.
2. **SPU jobs:** with the FIFO solved, the main thread waits for a WWS job manager queue (`FUN_000679F8`, queues at
   `0x01309E80 + n*0x40`). Only the `jm_gfx` workload ran the policy module, and no job module was ever loaded. The next
   point.

New tools: `tools/ppm_sheet.py` (a contact sheet of frames), `PS3RECOMP_VK_DUMP=dir/f%06u.ppm` numbers its dumps,
`KZ_SAIL_DBG=1`, `RSX_ENGINE_PASS_DUMP` (ps3recomp's).

## Main menu (2026-10-14)

The game reaches the main menu and answers the pad (`GAME / OPTIONS / EXTRAS`, `Campaign`, `One / Two Player`), with the
background movie playing. Two changes unlocked it:

1. **Job modules:** the header has *four* `ila` before `entryOffset`, not one; the extractor cut 12 bytes off the start,
   and the 16-byte signature never matched the code the job manager loads (the job descriptor points at `0x5A3000`, and
   the extracted module started at `0x5A300C`). Fixed in `tools/extract_spu_modules.py`; the entry is now `0x4030` and 9
   `.word` remain instead of 128.
2. **More PRs on ps3recomp's `kz` branch:** #286 (PPU lifter: exact VMX, branch decoding, integers and FP), #287 (SPU:
   exact float semantics, interpreter fixes), #288 (SPU ELF discovery), #289 (the SPU interrupt bit) and #266 (resuming
   translated SPU code after the interpreter). #290, #291, #283, #284, #295, #296 and #243 conflicted with what was on
   the branch and stayed out.

The lift script redoes the whole translation (PPU, SPU, job modules and libsre) after any change to ps3recomp.

## Towards Level 1 (2026-10-14)

With `PAD_SCRIPT` the game goes from the menu to Campaign → difficulty → Level 1's opening movie
(`Movies/Level 1/Intro.avi`) → the loading screen. During loading it crashes:

- The main thread writes from `NULL+0x100` in `func_0018DE64` (pose matrices; `FUN_0018E0A4`). The pointer comes from
  the per-thread scratch stack in TLS (`r13-0x6FFC`), set up by `FUN_00011120` with `0x40007E70`.
- `PPU_WWATCH=0E000004` shows who zeroes that field: `FUN_003D4E80` writes a vector of 8-byte entries from `0x0E000000`.
  That address is the TLS block ps3recomp's runtime reserves (`ppu_sysprx.cpp`, `s_tls_next = 0x0E000000`).

Useful tools here: `gdb` running the port as a child (`ptrace_scope` blocks attaching), `PPU_WWATCH`/`PPU_WWATCH_LEN`/
`PPU_WWATCH_MAX`, and `GCM_RECDBG=1`.

### Update

- The crash came from the keyboard: `cellKbRead` (NID `0xFF0A21B7`) had no handler, returned 0 without filling the
  `CellKbData`, and the game (`FUN_003D4E80`, the debug keyboard read) read stack garbage as keys and overflowed the
  event vector over the main thread's TLS (`0x0E000000`). A patch: `cellKbRead` becomes an alias of `cellKbGetData`.
- After that, a deadlock: the HLE delivered the game's vblank handler (`func_00111078`) inside any HLE call of the main
  thread, and the handler waits in the same function (`func_00110D50`) the main thread was already waiting in. A patch:
  `cellGcmHleSetIsrThread(1)` (on in `stubs.cpp`) delivers vblank/flip/user commands on a thread of their own, like the
  PS3's graphics interrupt thread.
- Now the game goes through Level 1's opening movie and stays on the loading screen: the main loop is alive (it reads
  the pad), but flips almost stop and the job manager's policy module spins in `poll -> continue` with MFC DMA lists
  with stall-and-notify (`[mfc-list] RESUME-ELEMS`). The next point (MFC/SPU emulation in the runtime).

## Inside Level 1 (2026-10-14)

In a 25-minute run loading finishes and the game enters Level 1: the HUD shows right (crosshair, health, ammo 270),
but the 3D world is black (the same symptom as RPCS3) and the game runs at about 0.7 frames per second. Loading takes
about 8 minutes.

- Loading was not hung: stack samples show different SPU jobs each time (`jobmod_*` modules), just slow. The job
  manager's policy module spends the rest in `cellSpursModulePoll` (hundreds of millions of calls).
- Performance and the black 3D are the next points.
- `[mfc-list] RESUME-ELEMS` now prints only with `MFC_LIST_DBG=1` (it was printed at every list resume).
- Keyboard on Linux: with `PS3RECOMP_VK_WINDOW=1` the SDL window takes the keyboard as a pad (arrows = d-pad, Z/Space =
  X, X = O, C = square, V = triangle, Q/E = L1/R1, 1/2 = L2/R2, Enter = START, Tab = SELECT, WASD = left stick, IJKL =
  right).

### Black 3D: what the pass capture shows (2026-10-14)

`RSX_ENGINE_PASS_DUMP` inside Level 1 (frame about 6400, 540 s): each frame has about 390 draws in 4 passes.

- **Pass 0** (the opaque world): only 10 draws, pipelines 69 to 78 with 3 or 4 textures each (lightmaps and the like),
  depth writes on. Even so depth comes out almost all at 1.0 (far): the geometry barely covers any pixels.
- **Passes 1 and 2**: post-processing at 640x360.
- **Pass 3**: 45 composition draws.
- **Pass 4**: about 340 transparent draws (`SRC_ALPHA` blend), no depth writes: HUD, particles and decals. The HUD is
  right.

So the world reaches the RSX with few draws and almost no coverage, which points at the geometry's vertices or matrices
(probably made by `jm_gfx`'s SPU jobs), not at the shader. The build with `-march=x86-64-v3` reaches Level 1 in under
10 minutes.

### Level 1's world appears (2026-10-14)

Two causes, both in ps3recomp:

1. **Occlusion reports in main memory.** The game reads its occlusion queries with
   `cellGcmGetReportDataLocation(index, CELL_GCM_LOCATION_MAIN)`. The HLE ignored the location and answered from the
   local table (0 pixels): the game culled almost the whole world. `GET_REPORT` also wrote the report at the wrong IO
   offset; the right one is `0x0E000000 + offset`, as in RPCS3. After that the world pass has about 330 draws per frame
   (before about 10).
2. **A pass that reads its own target.** The scene's last draw is a full-screen colour-correction pass: it samples the
   frame itself (`fA5`, the target's own address) through a 256x1 ramp and writes to the same place. The engine treated
   the read of the current target as an ordinary texture and read the bytes from game memory, which the host never
   wrote (black). Now it writes the target to game memory before that draw and samples the copy.

How it was found: `RSX_ENGINE_PASS_DUMP_EVERY=25` writes the target every 25 draws; with `RSX_ENGINE_PS_DEBUG=1`
(magenta shader) the world showed in every intermediate frame and vanished only at the last draw. New debugging tools in
that patch: `RSX_ENGINE_PASS_DUMP_CONSTS=lo,hi`, `RSX_ENGINE_PS_DEBUG=1..4`,
`RSX_ENGINE_NO_BLEND/NO_STENCIL/NO_DEPTH`, `RSX_ENGINE_SKIP_PARTIAL_CLEAR`, `RSX_VK_DRAW_STATS`; the pass dump's
`draws.txt` now has depth/stencil state, texture bytes, clear masks and the first vertex.

### Level 1 performance: from about 1.2 to about 4.9 fps (2026-10-14)

No `perf` on this machine; `tools/stack_sample.sh` samples stacks with `eu-stack` (the port must run with
`KZ_PTRACER=1`, which allows `ptrace` despite `ptrace_scope=1`) and `PS3_POLLTOP=5` lists the `sys_timer_usleep` call
sites.

- **Before**: the CPU almost idle. The main thread spent about 64 % in `usleep(100)` at `0x679F8` (called from
  `0x110CE0`): it waits for a WWS job list to finish (per-SPU progress counters at `+0x42..+0x4E`). The SPURS kernel
  thread slept 1 ms per pass, backing off up to 16 passes when the module found no work. Job lists are not "kicked", so
  each waited up to about 16 ms, and the game sends dozens per frame.
- A patch: `PS3_SPURS_POLL_US` (the pass period) and `PS3_SPURS_IDLE_MAX` (the back-off cap); with a period under 1 ms,
  a pass that found work goes straight into the next without sleeping.
- A patch: an AVX2 fast path for SPU `fa/fs/fm/fma/fms/fnms` (a port of the NEON path), bit-identical to the references
  (the test `runtime/spu/tests/test_spu_float_neon.c` now runs on x86 too).
- **Now**: the SPURS thread is about 85 to 90 % busy and is the bottleneck; about 60 % of its samples are in the
  recompiled SPU code (the `jobmod_*` jobs).

### SPURS on several threads, a window to play in (2026-10-14)

- A patch (`PS3_SPURS_KERNEL_LANES=1`, on in `stubs.cpp`): one kernel thread per SPU of the instance. Killzone HD has two
  SPURS instances ("HiPrio" and "LoPrio") of 2 SPUs each and nine WWS workloads, all with `maxContention` 1. The threads
  take different workloads at the same time, each workload on one only. Level 1: about 5.5 fps.
- `SPURS_FORCE_SPUS=3` (already in ps3recomp) runs the same list on 3 "SPUs" and reaches about 6.9 fps, but departs from
  the hardware (`maxContention` 1) and with 5 the game hangs; kept as an experiment.
- Samples with the threads: the two SPURS threads are about 55 % busy and the main thread about 54 % waiting for job
  lists. The critical path is PPU → job list → PPU, in series; about 200 ms of SPU code per frame on one thread. The
  next real gain is faster recompiled SPU code (the lifter emits each instruction reading and writing `ctx->gpr` in
  memory). Tried without effect: `-O3` on the SPU code, `SPU_DEBUG_PROBES=0` (kept), the FIFO drain period
  (`KZ_RSX_POLL_US`) and SPURS polling without sleeping.
- **`play.sh`**: opens the game in a window (Wayland/SDL, tested), with no time limit; `KZ_FULLSCREEN=1` for full screen.
  Esc or closing the window ends it.

### Control inside the level (2026-10-14)

Symptom: the keyboard (and the pad) only worked in menus; in the level nothing answered.

- The game read the right input: the pad reader (`0x3D48F0`, a virtual method of the device) got the right sticks and
  buttons, and the player controller (`0x291FA8`) queried them every frame.
- The player's action map (the user profile, `*(*(ent+0x10)) + 0xF4`: button/axis codes such as `-101 100 102 -103 ...
  13`) was **all zero**: every action pointed at button 0. Menus use another path, with fixed indices.
- Cause: `cellSaveDataAutoLoad2` with no existing save. The game's callback answers `CBRESULT_ERR_NODATA` and ps3recomp
  returned `CELL_SAVEDATA_ERROR_NODATA`; the game's profile loader only creates the default profile when it gets
  `CELL_SAVEDATA_ERROR_CBRESULT` (as RPCS3 returns). A patch. Now the profile is born with the default controls and is
  saved under `gamedata/dev_hdd0/home/00000001/savedata/`.
- A patch: the virtual pad on port 0 started with the sticks at 0 (held up/left); now they start centred. `PAD_TRACE=1`
  shows the reads.
- An automatic test (no save, `PAD_STICK="128,0,255,128,640,760"`, R1 from 680 s): the player walks, turns, fires and
  reloads in the level.
- For tests with `PAD_SCRIPT`: delete the test save first (with a saved profile the menu flow changes) and do not press
  START during the level's movie: the pause request queues and opens the pause menu as soon as the level starts.
- `KZ_SAIL_TRACE=1` (in `sail.cpp`) lists each cellSail call and each event sent to the game.

### Towards 144 fps: a per-function profile (2026-10-14)

`KZ_PROF="<start s>,<seconds>,<file>"` (`port/kz1hd/prof.cpp`) samples each thread at 1 kHz of its own CPU time and
writes the mappings alongside; `tools/prof_report.py <file> <binary> --group` gives the table per thread and per
function (or per job module with `--group`).

Level 1, 30 s (about 5.7 fps):
- The two SPURS threads of the HiPrio instance are about 50 % busy each, about 1 core of SPU work in series: about
  180 ms per frame, the critical path. About 70 % of it is job code (`jobmod_14e0d6b1737e` 16 %, `af58da8c8c70` 10 %,
  `f60183d3d087` 9 %, `6b716cb6ea57` 7 %, `ab05f04899ea` 7 %, `adf3a4a70db6` 6 %, `85fc8580d65e` 3 %), about 9 % the job
  manager's policy module and the rest the runtime (DMA `spu_wrch`/`mfc_do_transfer`/memcpy about 13 %).
- PPU: about 11 % busy (the rest waits for job lists). RSX thread: about 12 % (memcpy, the NVIDIA driver, texture
  decoding).
- Patches: a fast rejection in `gcm_note_spu_put` (it was 3 %) and `absdb`/`avgb` in SSE (it was 4 %; `sync` becomes a
  compiler barrier on x86).

Conclusion: recompiled SPU code costs about 5x the real SPU per instruction (about 30 x86 instructions per SPU
instruction in the hottest loop) and runs in series. 30 to 60 fps need much better generated SPU code and more
parallelism; 144 fps needs the heaviest SPU jobs replaced by native code and the game logic (made for 30 fps) decoupled
from the frame rate.

### About 9.8 fps in Level 1 (2026-10-14)

- **A microbenchmark with real context**: the context (registers + LS) of a vertex job (`jobmod_14e0d6b1737e`,
  function `0x57E8`) was captured in the game and rerun 20,000 times in isolation; the output is compared by hash. It
  measures a change to the generator or helpers in seconds.
- A patch (`SPU_FAST_FLOAT`, on in the port; `KZ_SPU_EXACT_FLOAT=ON` turns it off): SPU `fa/fm/fma...` become one host
  IEEE instruction, with MXCSR in round-to-zero + flush-to-zero during SPU execution (which is what the SPU's float unit
  does). The captured job's output is bit-identical, 2x faster. Differences are possible only in the SPU's extended
  range above `FLT_MAX`.
- A patch: the about 60 integer and permute operations ps3recomp had vectorised for NEON only now work on x86 through
  `runtime/spu/spu_neon_sse.h` (a subset of NEON over SSE4/AVX2). Every SPU runtime test passes on x86. Captured job: a
  further 1.7x.
- A patch: the "poison" detector that read every PUT word by word becomes optional (`SPU_POISON_DMA=1`); a per-thread
  cache of the MFC slot.
- A patch: `fi`, `frest`, `frsqest` and the `cuflt/csflt/cflts/cfltu` conversions vectorised in AVX2 (the scalar ones
  become `*_ref` and the test `test_spu_conv_x86` compares both). Captured job: 25.7 to 23.1 ms; the game about 10 fps.
- Result: Level 1 from about 6.2 to **about 9.8 fps**. The SPU threads drop to about 42 % each.
- `tools/prof_lines.py <prof> <binary> <regex>`: the source lines behind a function's samples (runtime and SPU built
  with `-g`).

### Where the frame goes (about 11 fps, 2026-10-14)

- `KZ_PROF_WALL=1` (with `KZ_PROF`): samples wall time with the stack, so a thread parked in libc is charged to the
  function that waited.
- The picture: the PPU sleeps about 80 % of the frame, 62 % of it at `0x679F8` (waiting for a WWS job list) and 18 % at
  `0x355334` (waiting for an RSX label). `PS3_POLLTOP=4` shows the loops.
- `PS3_SPURS_WID_STATS=1`: time per workload. The game's 9 WWS workloads are created with `maxContention=1` (one SPU at a
  time by design); `jm_post` (wid 1) alone takes about 66 % of wall time and is the critical path. Forcing 2 SPUs
  (`SPURS_FORCE_SPUS=2`) makes it worse (about 9 fps).
- A patch: the engine's draws go into one command buffer (it was a submit + GPU wait per draw: 3800 → about 185 submits
  per second). It does not change fps today (the GPU is not the bottleneck), but it frees the RSX thread.
  `RSX_VK_WAIT_STATS=1` measures it.
- A patch: the per-workload statistics above.
- A patch: a shared write between SPU threads in `spu_depth_guard` (false sharing) became per thread; lockstep and
  per-step traces behind one check; MFC parameter channels (LSA/EA/size/tag) written inline; `mpy/mpys/mpya/mpyhh`
  through `pmaddwd`.
- Result: about 10 → **about 11 fps**. On the SPUs, about 58 % of the time is job code, about 10 % the policy module and
  about 25 % the runtime.
- The limit: with a serial workload of about 65 ms per frame, 144 fps needs it about 9x faster. Promoting SPU registers
  to locals did not change the microbenchmark. What is missing is a native rewrite of the heavy jobs (`f601`, `14e0`,
  `6b71`, `af58`, `ab05`) and decoupling rendering from the logic's 30 Hz.

### About 12 fps: SPU registers in local variables (2026-10-15)

- Real context capture: the heaviest `jm_post` loops (`f601` 5368/4E60/4FD0, `14e0` 57E8, `6b71` 5330/5880, `af58`
  7938, `ab05` 52B0/5AD8) are self-contained leaf functions. A microbenchmark reruns each on the state captured in
  Level 1 and compares registers + LS by hash.
- A lifter patch: in leaf functions (only register and LS operations, internal branches, `SPU_RET` and a direct exit
  through the trampoline) the registers become locals of the host vector type (`spu_vreg`), loaded on entry and written
  back on every exit. As a `u128` local, the union became two 64-bit integers and each operation went back and forth
  between general and vector registers. `shufb` now separates preparing the mask (which the compiler hoists out of the
  loop) from applying it (2 `pshufb` + OR). Result in the loops: 1.5x to 2.5x, identical output. 2,020 functions
  promoted.
- A patch: 16-byte LS reads and writes with one load + `pshufb`.
- A patch + `KZ_PROF`: the SPURS threads' samples carry the workload (`KZ1HD/w1`), separating the critical path.
- Result: about 11 → **about 12.2 fps**. `jm_post` falls from about 66 % to about 57 % of wall time.

### About 15 fps: native SPU loops (2026-10-15)

- `KZ_MARCH=x86-64-v4` (AVX-512: 32 vector registers, less spilling in the SPU loops) on this PC: about 13.1 fps. The
  default stays `x86-64-v3` for other machines.
- `port/kz1hd/native/`: native (AVX2) versions of the heaviest `jm_post` loops. The lift passes their names to the
  lifter (`SPU_LIFT_NATIVE`), which emits the recompiled body as `<name>_lifted`. The native loop writes memory; the
  output registers (the loop is software-pipelined) come from replaying the last iterations through the recompiled body
  with the pointers advanced. Cases off the pattern (sizes, overlap, different constants) fall back to the recompiled
  code.
  - `f601`: blend `B + A*(256-alpha)>>8` (7.7x), saturating add (12x), 2x2 9:3:3:1 upsample (2x).
  - `6b71`: a 7-tap vertical Gaussian blur [1 6 15 20 15 6 1]/64, with a per-channel weight in the second version (3x).
  - `ab05`: the same blur, horizontal (1.3 to 1.7x; short loops, the replay dominates).
- `KZ_NATIVE_VERIFY=1`: also runs the recompiled code on a copy of the context and compares registers + LS (and
  corrects). About 12 million calls in Level 1, 0 divergences.
- Tools: `tools/spu_capture.py` (captures SPU functions' entry state in the game) and `tools/spu_sym.py` (symbolic
  execution of the loop: prints each byte written as a formula of the bytes read).
- Result: **about 14.9 fps**. `jm_post` about 53 % of wall time; inside it, about 15 % DMA copies, about 14 % the
  `14e0` job (gather), about 17 % runtime dispatch.
- Patches: PPU stores pass one check of the diagnostic watches (they were about 16 % of the PPU thread) and read the SPU
  reservation bitmap inline; SPU DMA gets a fast path without the about 20 diagnostics behind environment variables;
  `PS3_USLEEP_MAX_US` (diagnostic). The RSX queue drained every 0.1 ms (`KZ_RSX_POLL_US`, it was 4 ms).
- `KZ_VBLANK_HZ` (default 62.5 then, a 16 ms tick): the game turns its frame on a vblank, so fps is that rate divided by
  a whole number of ticks. At 144 Hz Level 1 gives about 15.7 fps against about 15.4 at 62.5 Hz: the frame really costs
  about 62 ms; quantisation is not the limit.
- A patch (`PS3_SPURS_WID_LANES=<wid>:<n>`): a workload runs on up to n kernel lanes at once, each as a virtual SPU
  (spuNum = slot). `jm_post` is created with `maxContention 1`, but the WWS job manager runs right on the instance's 2
  SPUs and it is the critical path: the port turns `1:2` on by default. Level 1: **about 15.5 → about 21 fps**, a right
  picture, stable. 2 lanes on all 9 workloads changes nothing beyond that.
- Dropped: the `14e0` motion-blur loop (0x57E8, 14 samples per gather) native is bit-identical but slower than the
  promoted recompiled code, which already handles 4 pixels per vector.
- With the vblank at 144 Hz the game frame gives about 22 fps (about 45 ms). The PPU waits for the RSX queue (`0x35544C`)
  more than for the jobs; the RSX thread spends about 23 % waiting for the GPU (surface readbacks for the post-processing
  SPU jobs) and about 19 % idle.
- The game makes no time syscall per frame (`PS3_SYSCALL_HIST`); only semaphores and `usleep`. Even so, with the vblank
  at 144 Hz the character dies much earlier in the automatic test: part of the logic counts vblanks. A 144 Hz vblank is
  no good for play without decoupling the logic.

### About 27 fps: the RSX thread was the critical path (2026-10-15)

- `PS3_TRACE="<start s>,<seconds>,<file>"`: a Chrome/Perfetto JSON timeline with the PPU's waits (per call address),
  SPURS modules per lane, the RSX queue, GPU waits, texture uploads and hashes, readbacks and flips. With start 0 the
  port arms it itself when the game spends 10 s between 17 and 32 flips/s. `tools/trace_frames.py` summarises per frame.
- The trace showed the RSX thread processing the queue for about 37 ms of an about 48 ms frame (13 ms waiting for the
  GPU, 8 ms decoding and uploading textures), and the PPU waiting for it more than for the jobs.
- A patch: Killzone's colour-correction pass reads the target it draws; now it is a GPU copy of the target
  (`color_snapshot`) instead of readback + decode + re-upload. Texture uploads go into the Vulkan batch (before: a new
  buffer + a GPU wait per upload). Shader constants come from ordinary RAM (they were read from GPU memory).
- Result: Level 1 **about 21 → about 27 to 28 fps**, the picture checked in play (periodic dumps with
  `PS3RECOMP_VK_DUMP=<dir>/f%05u.ppm PS3RECOMP_VK_DUMP_EVERY=400`).
- Dropped (no gain or worse): inline PPU stores in the generated code (no gain; they hung loading without a compiler
  barrier, because the old call ordered the stores), 0 µs poll in the SPURS kernel, 2 lanes on every workload (the same
  as on `jm_post` only), and consecutive draws on the same target sharing a render pass (no gain: the GPU is not the
  limit).

### The game has a 30 fps limiter (2026-10-15)

- With all drawing off (a temporary test in `sink_end`), Level 1 holds **30.0 fps**, with the emulated vblank at 62.5 Hz
  or 240 Hz and with 2 or 3 lanes on `jm_post`. With drawing, 26 to 29 fps. So the port's faithful target is **a steady
  30 fps**, not 31.
- The limiter is the game's clock. `func_0021C0F8` reads the time in ms (`func_0001CC20`), multiplies it by the double
  `0.03` at `0x21C0F0` (1/30 s ticks) and repeats until at least 1 whole tick has passed; the delta in ticks is capped by
  the clock object's `+0x74` field. `func_00219320` writes `1/30` (seconds per tick) to `+0x54`.
- Evidence: a 2x guest timebase lifts the ceiling to about 35 fps without drawing (the CPU limit), but the game runs 2x
  fast. Changing only `+0x54` to 1/60 changes nothing. 60 Hz ticks (`0x21C0F0` = 0.06) with `+0x54` = 1/60 give 27 to
  30 fps with drawing and the player survives much longer in the test without input. The logic was not at the right
  speed, so going past 30 needs more than changing the constant.
- Also dropped: the Vulkan batch sent in pieces without waiting for the GPU + clears inside the batch (the GPU wait fell,
  fps the same).
- Without drawing, the PPU spends about 80 % of the frame computing. To truly pass 30, besides the tick, the PPU has to
  get much faster.

### Towards a steady 30 fps (2026-10-15)

- Patches: linear A8R8G8B8 decoded per row (the post-processing SPU output at 1280x720, re-uploaded every frame, fell
  from about 1.5 ms to about 0.2 ms) and depth written to guest memory in whole words (the same output, NaN and
  infinities included). Alone they do not move fps (27.2 to 27.7).
- A chain of an about 36 ms frame: the PPU computes about 24 ms, waits about 8 ms for SPU tasks (`0x67A80`) and about
  4 ms for the RSX (`0x35544C`). The SPU wait does not shrink with 3 or 4 lanes on `jm_post`.
- Dropped: clears inside the Vulkan batch (about 22 fps, because the clear with a wait was what made the GPU start
  early), no full barrier between draws (RSX thread from about 19 to about 14 ms per frame, fps the same), the RSX going
  on past the flip (the same), the vblank at 59.94 Hz (the same).
- `tools/trace_frames.py` now merges each flip's two presents (before it counted frames twice).
- A patch: a cached texture whose contents changed is reloaded into the same Vulkan texture (before: a new texture +
  freeing the old one, two GPU waits at the end of the frame). Level 1 at 62.5 Hz: about 27.5 → about 28.5 fps.
- A patch + `PS3_SPURS_WID_REPEAT=16` (the port's default): `jm_post` returns to the kernel every few jobs, and each
  lane spent about 22 % scanning the other eight empty workloads between quanta. Re-running the workload that found work
  brings Level 1 to **about 29.7 fps, nearly every second at 30** (62.5 Hz and 59.94 Hz), the picture checked.
- To measure small gains, use `KZ_VBLANK_HZ=240`: at 62.5 Hz each frame becomes 32 or 48 ms, and a 2 to 3 ms gain does
  not show until the frame drops under 32.

### A locked 30, movies and input lag (2026-10-15)

- **Pacing:** the flip now shows only at the vblank (before, whenever the queue reached it) and the default vblank
  became 59.94 Hz, the PS3's. Two vblanks are one 1/30 s tick of the game clock. In Level 1, 98 to 99 % of frames come
  out exactly every 33.4 ms (before: spread between 27 and 41 ms).
- **Hitches of 70 to 250 ms:** new shaders compiled in the middle of the level. A patch: an on-disk cache of SPIR-V and
  Vulkan pipelines (`$PS3RECOMP_VK_CACHE`, default `~/.cache/ps3recomp-vk`). The second time nothing is compiled.
- A patch: the window is presented without waiting for the GPU (its own command buffer and fence), in MAILBOX mode when
  available, and the frame only comes back to the CPU for a dump.
- A patch: window events (keyboard) are pumped every about 1 ms during the RSX queue. Before, the pad read could see the
  keyboard up to about 20 ms late.
- **Input lag in menus and stutter in movies:** Killzone HD does not wait for its flips. In menus and in the opening
  movie it got up to about 600 frames (about 10 s) ahead of the screen, queued in the command ring. The movie only took
  frames in bursts every about 400 ms (about 11 % dropped). A patch: at most 2 pending flips
  (`PS3_GCM_MAX_QUEUED_FLIPS`). The movie now takes each frame on time (0 dropped) and the game answers the pad on the
  next frame.
- `Intro.avi` is MPEG-4 at 25 fps; on a 60 Hz screen that gives a 2-3 vblank cadence per frame, as on the PS3.
- `[kz-stats]` shows `queued=`: flips the game wrote that the queue has not reached yet.

### Loading, resources and CPU (2026-10-15)

- A patch: `sys_lwcond_wait` was a 1 ms poll and signals were no-ops, so each hand-off between the game's threads cost
  about 1 ms. Level 1's load fell from **about 42 s to about 8 s**, and the 7 idle threads went from 1000 to 100 wakeups
  per second.
- A patch: the pipeline cache is written on its own thread (before, in the middle of the frame).
- Use measured in a session: CPU about 1.6 to 2.1 cores in play (PPU about 60 %, 2 SPU lanes about 58 % each, RSX about
  28 %), about 0.35 in the menu; GPU 25 to 35 % (RTX 3070 Ti, low clock); 244 MB of VRAM; about 800 MB of RAM, growing
  about 1.3 MB/min (the malloc heap grows only about 0.3 MB/min).
- Dropped: inline PPU stores in the generated code (the binary's text went from 59 to 101 MB and the PPU spent more CPU,
  not less), a per-thread cache in the SPU function lookup (gain within the noise), a 4-way texture hash (the memory
  read is the limit).
- On the SPU lanes, about 39 % of the time is infrastructure (DMA copies about 11 %, dispatch and per-step checks about
  15 %, the job manager's policy module about 9 %); the rest is job code (the biggest: `jobmod_14e0` `57E8`, about
  15 %).
- A patch: `PS3_TIMEBASE_SCALE=<n>` makes the guest clock run n times faster. With 3 and a 240 Hz vblank the 30 fps
  limit gets out of the way and the real ceiling can be measured: about 35 to 36 fps in gameplay (an about 28 ms frame:
  about 18 ms of PPU plus chained PPU, RSX and SPU waits).
- Also dropped: PPU at `-O3`, PGO (the instrumented build hung at 6 fps then), the GPU forced to its top clock (no gain;
  the GPU is not the limit).

### About 1 s of input lag in menus after a few minutes (2026-10-15)

- Cause: the audio, not the input. On Linux the `cellAudio` mixer produced blocks faster than SDL played them, and the
  device queue grew about 1 s every 20 s (7.7 s after 150 s). The menu's click sound came later and later. The key
  reached the game in the same millisecond, and the selection on screen changed in about 140 ms all along.
- A patch: the mixer only mixes when the queue has room for a block. The queue stays at 1024 samples (21.3 ms).
  `AUDIO_QUEUE=1` shows the queue every about 5 s.
- A patch: `PAD_KEYLOG=<file>` (`1` = stderr) logs when the keyboard changes and when the game reads the pad, to measure
  end-to-end latency.

### CPU beyond 30 fps: runtime overhead (2026-10-16)

Measured with `PS3_TIMEBASE_SCALE=3` and a 240 Hz vblank (mean of 40 samples in gameplay; noise about 0.4 fps).
Starting point: about 35.4 fps.

- The uncapped frame (about 28 ms): the HiPrio instance's two SPURS lanes are about 78 % busy, nearly all of it in
  `jm_post` (`PS3_SPURS_WID_STATS`: about 1.25 lanes continuous); the PPU about 58 %; the RSX thread about 41 %. The
  limit is the SPU lanes' throughput: speeding up only the PPU barely changes fps.
- A patch: the bitmap of lines reserved by an SPU was never cleared, so every line an SPU had once reserved sent every
  PPU store on it through the lock and the context scan: 784 million in a 4-minute run, almost none with a live
  reservation. Now the scan clears the bit (46 million). The context registry had 8 slots and the game reaches 12; the
  extra ones did not get the lost-reservation event.
- A patch: the "stack reset" check ran on every SPU trampoline step with no entry registered, and `spu_run_with_halt`
  called `getenv` on every job.
- A patch: the depth limit is tested inline in the drain loops (it was a call per entry).
- A lifter patch: a direct branch from one recompiled SPU function to another handed the target to the trampoline and
  returned, and the drain loop made the call. The `jm_post` jobs made about 14 million such steps per second. Now the
  lifter emits `SPU_TAIL`, a guaranteed tail call (clang's `musttail`, the stack does not grow). It still goes through the
  trampoline with an SPU interrupt pending, with a per-step probe armed (`SPU_NO_TAIL=1` forces it) and for branches to
  LS 0. `SPU_LIFT_NO_TAIL=1` generates the old form. The picture checked in Level 1.
- Result: **about 35.4 → about 38.8 fps** uncapped (+10 %).
- The timeline of an uncapped frame (`PS3_TRACE`): the two SPU lanes work about 17 ms together while the PPU runs about
  10 ms and then waits for the jobs; then the PPU runs alone about 6 to 9 ms (SPUs idle) and waits for the RSX queue
  about 2 to 5 ms. The three stretches are serial. Speeding up PPU stores (about 25 % of it, a leaf function with no
  frame) did not change fps: they are memory-bound. `KZ_PROF_TS=1` puts the time on each `KZ_PROF` sample, to split the
  PPU profile by phase.
- Dropped this round (no measured gain): `__restrict` on the PPU functions' context, the PPU store as a leaf function, a
  reordered decode in the SPU stub micro-interpreter, `PS3_SPURS_WID_REPEAT` 64/256, `shufb` with AVX-512 VBMI
  (`-march=native`). The `14e0` 57E8 motion-blur loop reads about 14 quadwords per pixel at computed addresses; the
  recompiled code is already close to optimal.
- A rare hang, open then: in 1 of about 20 runs (a `-march=native` build), entering Level 1 the main thread stays in
  `EventFlagWait` (flag `0x401F3980`) while task 0 of taskset `0x401F2080` sleeps in `WAIT_SIGNAL`: a signal lost between
  the LF queue and the task. It did not reproduce in 4 runs in a row of the default build. (Later: no longer reproduces,
  0 in 22 loads, 2026-10-17.)
- DMA volume in gameplay: about 2.9 million GETs and about 2.1 million PUTs per second (about 1.4 GB/s and about
  1.1 GB/s), about 85,000 GETs per frame. The copies are about 14 % of the lanes; each transfer's fast path is already
  lean.
- What is left on the lanes is `jm_post`'s job code (post-processing: `14e0` 57E8 about 13 %, `af58` 7938 about 4 %,
  and a long tail) and runtime dispatch (about 12 %). 60 fps needs about 1.6x more of that throughput.

### Towards 144 fps: jm_post on 3 lanes (2026-10-16)

- A patch (`PS3_SPURS_INSTANCE_LANES=<prefix>:<n>`): the instance gets n kernel threads instead of nSpus, and
  `PS3_SPURS_WID_LANES` may go beyond that. The port turned on `HiPrio:3` and `1:3` (`jm_post` on 3 virtual SPUs).
- Why 3: the WWS job manager keeps a 16-byte line per SPU at `+0x40 + 16*spuNum` of the workload data, and the list's
  ticket line is at `+0x70`. With 4 lanes SPU 3 writes over the tickets and the level load hangs; with 6 there are DMAs
  to garbage addresses. 4 lanes on every instance hang too.
- Result: **about 38.8 → about 44 fps** uncapped (4 of 4 runs stable), the picture checked, a locked 30 in real time.
- A patch: each draw opened its own render pass behind a full barrier, and the GPU drained between draws (about 365 per
  frame). Consecutive draws on the same framebuffer now continue the open render pass, without a barrier, unless they
  sample one of its attachments; any other command in the batch closes the render pass first. **About 44 → about
  45.5 fps** uncapped, the picture checked.
- The uncapped frame (about 22 ms): SPUs about 11 ms, then the PPU alone about 5 ms, then the RSX about 5.5 ms, of which
  about 65 % waiting for the GPU. The GPU stays in P5 (core about 600 to 850 MHz, memory 810 MHz) because the driver sees
  it barely busy.
- Dropped: clearing colour/depth inside the batch with an asynchronous submit (two command buffers) did not change fps
  measurably.
- A PPU lifter patch: stores through the stack (`r1`: register saves in the prologue, spills, locals) become inline
  (`vm_write32/64_stk`), without a call; they were about 98 % of the 64-bit stores. They stay out of line if a store
  diagnostic is armed (`vm_inline_st_ok`); `PPU_LIFT_NO_INLINE_STK=1` generates the old form. About 44.0 → about
  44.9 fps.
- A PPU lifter patch: `ppu_fp_bin`/`ppu_fmadd_core` first test whether the result is NaN (a NaN operand always gives a
  NaN result), and only then the operands. Identical results; about 44.9 → about 45.4 fps.
- Dropped (within the noise of about ±0.7 fps): the present blit without waiting for the GPU, a fast path for PPU
  indirect calls.
- A patch: hooks for the port to observe each SPU's transfers (`g_spu_dma_hook`) and each module job's entry
  (`g_spu_job_hook`, LS `0x4030` with the overlay resident). Used by `port/kz1hd/native/jm.c` to check native versions
  of the `jm_post` jobs against the SPUs.
- `jm.c`: a PC executor for `jm_post` (`KZ_JM_HLE=3` by default then), alongside the 3 lanes; details in
  [wws-jm-post.md](wws-jm-post.md). About 44.4 → about 48.4 fps at the start of gameplay, about 52-53 → about 55-57
  later.
- The timeline (`PS3_TRACE` with the executor's jobs as `jm_job`): with post-processing in parallel, the frame's
  critical path is the PPU (about 15 ms of work, without waiting for `jm_post`: the game's wait loop,
  `func_00067650`, sleeps 100 us per turn and almost never runs), followed by the RSX's tail (about 5 ms).
- Dropped: `jm_post` without the lanes and only PC threads (6 threads: about 47 fps, worse than 3 lanes + 3 threads),
  batches of several jobs per run of the private job manager (worse), and inline PPU stores off the stack (the
  generator's `vm_write*_inl` with an SPU reservation test; about 49 → about 46 fps, the bigger code costs more than the
  call).
- PGO (clang) of the translated PPU code: `pgo.sh` builds an instrumented build (`KZ_PGO_GEN`, PPU code only:
  instrumenting everything had the lanes fighting over the counters, about 6 fps), plays about 10 min, merges the
  profile and rebuilds with `KZ_PGO_USE`. **About 49 → about 55.6 fps** uncapped (mean of 4 runs, 53.6 to 58.6), about
  60 in the next section. Redo it after every PPU lift: a profile of other code is silently ignored.
- With a faster PPU, the RSX thread closes the frame (about 14 ms of CPU + about 4.5 ms waiting for the GPU per frame). A
  patch: the texture mutation hash (an FNV multiply chain per texture per frame, about 11 % of the thread) uses 4
  independent chains; colour and depth readbacks and depth snapshots reuse a staging buffer instead of creating, mapping
  and freeing one per call (the driver did mmap/munmap each time). About 55.6 → about 61.5 fps (2 runs: 64.1 and 59.0).
- A patch: depth read as an A8R8G8B8 texture (twice per frame, in the half-resolution passes) was read back from the GPU,
  written to guest memory and re-uploaded as a texture, with a full GPU wait each time. Now a compute shader
  (`rsx_vk_depth_pack.comp`) builds the same texels on the GPU, inside the batch. With `jm_post` skipped (`KZ_JM_SKIP`,
  about 60 → about 64): **about 64 → about 76 fps** uncapped.
- A patch: colour and depth clears recorded in the open batch (with a barrier before and after) instead of waiting for
  the GPU twice each. Before it changed nothing; without the depth round trips, about 76.0 → about 77.8 fps (3 runs).
- Real time with `play.sh`: gameplay holds a locked 60, limited by the game's vblank (59.94). With
  `KZ_TICK_HZ=144 KZ_GUEST_VBLANK_HZ=144`: about 67 to 80 fps, limited by the PPU (about 11 ms per frame building the
  draw, about 14 % in `vm_write32`, which is already lean: the cost is the access to game memory itself).
- Dropped: PPU registers in local variables in the generator (GPRs loaded on entry, written before each call and reread
  after per the PPC ABI). Without PGO: about 62.5 → about 55 fps; only in leaf functions: about 57.6. Probably register
  pressure and extra loads/stores at the boundaries. (A later, different approach worked: `ppu_regcache.py`, below.)
- Dropped (no measured gain, about 73 to 76 fps): two frames in flight on the GPU (the present without waiting for the
  GPU; the waits vanish from the timeline, fps does not change), draining the FIFO every 20 us (`KZ_RSX_POLL_US`) and
  flips at 1000 Hz (`KZ_VBLANK_HZ`; one run hung). The main PPU thread is about 97 % busy (only 2.8 % in `usleep`): the
  frame is limited by the game's code on the PPU. Sampling stacks with `eu-stack` misleads here (it showed 74 % in
  `usleep`, an effect of stopping threads to sample them).
- A PPU lifter patch: inline guest memory reads (`vm_read*_inl`) become `always_inline`; the compiler moved them out of
  line in large functions (about 11 % of the main thread in calls). About 73.5 → about 76 fps (3 runs). Also dropped:
  inline 32-bit stores with PGO (74.4 against 76, 3 runs) and the PPU clock (`mftb`) from the TSC instead of
  `clock_gettime` (no measurable gain).
- A PPU lifter patch: `--setjmp`/`--longjmp` (in the lift: 0x3B5CB0/0x3B5DD4, the game's libc setjmp/longjmp).
  Translated literally, longjmp restores the guest registers and returns to its caller, not to the setjmp, and the host
  stack keeps every frame in between. The Lua 4.0 of the level scripts raises its errors this way: the first error (New
  Allies, after the first checkpoint) corrupted memory and crashed the game. Now the setjmp call goes through a host
  `setjmp` in the translated frame, and longjmp ends in a host `longjmp` to it. Test: `tools/test_lifter_setjmp.py` (in
  ps3recomp, with the patch).
- A cellPad patch: the mouse on the right stick in the SDL keyboard fallback. A click in the window captures the
  pointer. Mouse speed becomes stick deflection (`PAD_MOUSE_GAIN`=10, `PAD_MOUSE_DECAY`=0.5 per 4 ms read,
  `PAD_MOUSE_DEADZONE`=20 to beat the game's dead zone); left button = R1 (fire), right = L1, middle = R3.
  `PAD_NO_MOUSE=1` turns it off. Direct mouse on the camera angles came next (below).
- Input lag: the game stayed 2 frames ahead of the screen (`PS3_GCM_MAX_QUEUED_FLIPS`=2, about 33 ms at 60 fps).
  `play.sh` uses 1 now; 60 fps holds (Level 1, 20 s: 60 per second either way).
- Direct mouse look (`port/kz1hd/pcoptions.cpp`): `func_00291FA8` is the player controller's update; it reads the right
  stick, applies `YawSensitivity`/`PitchSensitivity` (profile `+0x140`/`+0x144`), the response curve and the turn ramp,
  and leaves the rates at `+0xA8` (yaw) and `+0xAC` (pitch). The player update turns 2.27 rad/s per unit (measured on
  frames: rate 0.92 = one turn in 3 s); `+0x3C` is the rate before the 0.93 scale and, zeroed, stops the turn. The mouse
  adds its own rate after all that: counts × 0.022° × sensitivity, divided by the tick. The game's sliders stay the
  controller's. `PAD_MOUSE_STICK=1` goes back to the mouse on the stick.
- Mouse options in the game's menu: Pause > Controls > Advanced Settings is Lua 4.0 (`MenuScript.Pause`) with sliders
  bound to reflected attributes of the `ControllerConfig` class (`0x131E4DC`, registered in `func_002946D4`). The port
  adds three attributes (`MouseYawSensitivity`, `MousePitchSensitivity`, `MouseInvert`) with host getters and setters
  (fake addresses `0x100+`), and runs a piece of Lua (`lua_dobuffer` = `func_0007043C`, called from inside the generic
  binding `func_00185100`) that grows the list to 10 rows and adds the three items. The values live in `kz1hd_pc.ini`
  in the run folder, outside the game's save. Slider at 0.5 = 1x (0 = 0.1x, 1 = 10x).
- Menus with the mouse (`pcoptions.cpp`): with no player tick for 150 ms (menu, movie, loading) the pointer is free. The
  menus pick in list boxes (pause; `AddListBox` = `func_00275978`; `+0x38` 1.0 when visible, `+0x50..+0x5C` the
  rectangle, `+0x104` items, `+0x128` item height, `+0x12C` selected) and in selections (front end; `CreateSelection` =
  `func_0026D03C`; `Selection:Update(previous key, next)` = `func_00276DC0` runs every frame for the active selection
  only and names the keys: JOY_LEFT/RIGHT/UP/DOWN = 0..3). The menus' layout is 512x450. The item under the cursor
  becomes the target and the menu is led to it with the d-pad, one press at a time, so the game's handlers run (sound,
  the tab marker). Left click = X, right = O, wheel = up/down (or the active selection's keys), dragging with the left =
  left/right (sliders). `PAD_MOUSE_POS=x,y,w,h` simulates the pointer in tests.
- Keyboard and mouse: a key map in a file (`PAD_KEYMAP`; the port writes `kz1hd_keys.ini` in the run folder with the
  maintainer's layout on the first run). Lines `TARGET = input, input`: targets are the PS3 pad's buttons and the stick
  directions (`LS_UP`...), inputs are SDL key names ("E", "Left Ctrl"; "Ctrl"/"Shift"/"Alt" = either side),
  `MouseLeft/Right/Middle`, `Mouse4/5`, `WheelUp/Down`. Mouse and wheel only count in play; in menus they are the
  pointer's. Esc pauses (`PS3RECOMP_ESC_QUITS=0`); closing the window quits.
- More with the mouse: the `DialogBox` (`func_00272670`) is modal, so the menu logic runs on its own thread every 8 ms.
  The dialog is at menu `+0x19C`; the button labels at `+0x128` (count `+0x12C`, 0x64 each: `+0x08` width, `+0x58` x,
  `+0x60` the code a click gives, 200, 201...) and the answer the loop waits for at `+0x58` (-2 until there is one).
  Clicking a button writes its code there. Option rows (`AddAttributeEdit(HD)` = `func_002750D8`/`func_00275200`,
  `AddAttributeSlider(HD)` = `func_00275330`/`func_00275460`) take the list row in their own rectangle: clicking a value
  that toggles = right; the wheel over any option = left/right. `PAD_MOUSE_POS_FILE` (with `PAD_MOUSE_POS`) gives
  position and buttons in tests.
- Silent after the opening: MultiStream (SPU image 3, `spu_0001_at_0056FC80`) plays the music with the firmware's MP3
  decoder, `flashMP3.pic`, which the game reads from `/dev_flash/sys/external` and loads into LS at 0x1A880 (the entry,
  0x74A0 in the file, is called at LS 0x21D20). Untranslated, the audio task ended at the first music track ("branched
  into unlifted LS 0x21D20") and all sound stopped. The lift rebases the ELF (`rebase_spu_elf.py ... 0x1A880`) and
  translates it into `<SPU_DIR>/flashmp3`; `stubs.cpp` registers the functions in image 3 (`KZ_NO_MP3=1` turns it off).
  `AUDIO_RATE=1` shows the silent fraction every 5 s.
- A hang with sound on: with music playing, two producers (MultiStream's loader and the level streaming) push requests
  into the same Edge zlib LF queue; the `lwarx`/`stwcx.` reservation compared only the word, both commits went over each
  other's reads, the inflate task got a corrupt request (`edgezlib_inflate_queue.cpp:425`) and the main thread waited
  for flag `0x401F3980` forever. A patch: between PPU threads the reservation covers the 128-byte line (a per-line
  version bumped at every successful store-conditional; the lifter emits `ppu_resv_snap` at `lwarx`). SPU commits stay
  on the value compare: including them hung the PPU on a job manager line that changes all the time. Before: a hang in
  30 to 60 s of play in all 4 runs; after: 4.5 min without a hang, continuous sound, 60 fps.
- A hang in Strange Company (level 4) with sound on, the same Edge zlib failure: `_cellSpursLFQueuePushBody` returned
  `0x80410905` because the wake signal went out with a garbage task id. `stubs.cpp` ran libsre through
  `ppu_guest_call`, which uses a fixed auxiliary stack per thread: a push made from inside another guest call
  (MultiStream's loader does this) overwrote its frames. Now libsre runs on the calling thread's own stack, 4 KB below the
  frame, with the active context switched (store-conditionals check `g_active_ctx`). Level 4: 2 runs of 4 min without a
  failure, continuous sound, 60 fps (before: a failure in about 45 s). `KZ_LFQ_WATCH=1` shows SPU PUTs on the queues'
  header.
- Silent cutscenes: the game's "Video Player Audio" thread takes each block with `SoundAdapterGetFrame` and mixes it
  into port 1 with `cellAudioAdd2chData`, which did not exist in the runtime (NID 0x9E4B1DB8 unresolved: the call did
  nothing). A patch: `cellAudioAddData`/`Add2chData`/`Add6chData`, each call into the block after the last filled one.
  `sail.cpp` delivers one block per 256 frames of movie time (the game asks much more often than that). The intro and
  Movies/Level 10/Intro.avi: 0 % silent blocks (before 100 %). `KZ_SAIL_LOG=1` logs the audio requests.
- Video options (`pcoptions.cpp`): `kz1hd_pc.ini` in the run folder gives `display` (window, borderless, fullscreen),
  `resolution`, `refresh`, `vsync` (on, off, adaptive) and `fps_limit` (30, 60, 144), read at the start of `main` and
  passed to the runtime as environment variables (a variable already set wins). The window can be resized and keeps its
  aspect (black bars); Alt+Enter toggles fullscreen.
- Controllers: each controller takes the first free port when connected and keeps it; keyboard and mouse add to port
  0's controller instead of only counting without one. `controllers = merged` (default) in `kz1hd_pc.ini` makes any
  controller player 1 (`PAD_MERGE=1`), with rumble on all. Buttons by position (the bottom one is always X, on the
  Nintendo layout too); DualShock 4 and DualSense rumble over Bluetooth too. `pad_active_device()` says which family the
  player touched last. `PAD_VIRTUAL=xbox|ps5|switch` with `PAD_VIRTUAL_FILE` creates a virtual controller for tests.
  Tested with an 8BitDo Ultimate 2 (X-input) and a virtual DualSense.
- Button icons by device (`glyphs.cpp`, the `rsx_texture_seen_hook`): the menus draw buttons from two DXT5 textures, the
  `imagemapsmall` image font (1024x128; captions such as "O Back" and dialog buttons; the font's glyph table gives the
  rectangles) and the `menu/pause/textures/buttons` strip (512x32, 20-texel cells, for `<buttonimage name=Cross>`). The
  hook recognises the game's copies by a hash of level 0 and rewrites them with the set of the device in use: the
  original for PlayStation, letters and LB/RT for Xbox and generic pads, Nintendo positions, keys or mouse for the
  keyboard (the first input `kz1hd_keys.ini` binds to the button). The glyphs are drawn in code, at 4x, and re-encoded
  as DXT5; nothing of the game goes to disk. The switch is live. `KZ_GLYPHS=0` turns it off, `KZ_GLYPHS_DUMP=<dir>`
  writes the sets.
- The "PC" page (`pcoptions.cpp`): a "PC" item in Options (above Audio) and in Pause (after Controls), with display
  mode, resolution (the monitor's modes), V-Sync, frame-rate limit, controllers and mouse. The rows are new attributes of
  the `gGameSystem` class (`0x131ECD0`, category "PC settings"), which the menus read and write directly on the object;
  profile attributes (`ControllerConfig`) go through a copy of the values in the profile and do not call the getters.
  Choice values of a float attribute must be written as "1.0" (that is how the value comes back as text). Everything
  applies at once: window and swapchain through the Vulkan backend (`rsx_vulkan_set_display`/`set_present`, applied on
  the window thread), the game clock through `g_kz_tick_hz`/`g_kz_guest_vblank_hz` in `main.cpp`, controllers through
  `pad_set_merge`. The pause script has already built the menu when the Lua piece runs, so it redoes the items.
- Internal scale (`scale` in `kz1hd_pc.ini`, the "Internal Resolution" row on the PC page, applies on the next start):
  the Vulkan backend creates colour and depth targets `f` times the guest size, scales viewport and scissor, and divides
  the fragment program's WPOS by `f` (HLSL text before translation). What crosses guest memory (seeds, 2D transfers,
  colour and depth readbacks) is resampled at guest size on the CPU. The display image is scaled too. Level 1, 60 s of
  play: 0.5x 60.6 fps, 1x 60.5, 2x 58.0. The game is limited by the PPU; the 2x cost comes from the RSX waiting for the
  GPU at every submit (`vk_flush` is synchronous).
- A faster 144 mode (2026-10-20). The main thread waited about 10 % of the time for the RSX (`func_00355334` waits for a
  command-ring label). `frame_clock` only drained the FIFO up to the next flip every `KZ_VBLANK_HZ` tick, so at 144 Hz a
  frame that overran a tick waited for another: the 144 Hz clock stayed near 72 fps. Now, in play at 144, the cadence
  rises to 1000 Hz (`g_kz_flip_hz`, set by `menu_mouse` when there is a player tick; `KZ_FLIP_PLAY_HZ` changes it).
  Menus, movies and loads stay at 144: they do not follow the game clock and ran at about 900 fps. Level 1, in-play
  sections only: 79.9 to 89.1 fps; the 60 mode stays at 60. With a window, V-Sync "On" became MAILBOX (no tearing, and a
  frame that misses a refresh does not wait for the next); FIFO was stuck at about 72 to 77 fps and became the
  "FIFO (VRR)" option. The game's tick count (`ticks=` in `KZ_STATS`, the clock's `+0x6C`) confirms 144 ticks per second
  of game time.
- A freeze in the 144 mode in combat (level 3, about 6 min): the main thread waited forever for `jm_post`'s semaphore
  (object `0x1309EC0`, counter `+0x3C` at 0, the loop `func_000679F8` with `sys_timer_usleep`). In the header's queue 3
  (`0x40149DF0`) the ticket (663) had fallen behind the base (688), and no job was taken again. The PC threads serving
  `jm_post` (`native/jm.c`) update the header under the lock-line lock with plain stores, but no SPU reserves that line
  (the lanes do not run `jm_post`), so the PPU's stores and store-conditionals on it did not take the lock and a game
  update was lost in the middle of the read-modify-write. A patch: `spu_coh_pin` keeps the line's bit on; `jm.c` pins the
  header and the barriers. After: 2 runs of 10 min in combat without a hang.
- Opaque foliage (level 3's park, level 1's bushes) and then characters with empty parts: `cellGcmSys` sent the draw
  engine every FIFO method with the subchannel erased, and the 2D objects' methods (NV3062 `SET_COLOR_FORMAT` 0x300,
  `SET_PITCH` 0x304...) landed on the 3D registers of the same address, among them the alpha test (`0x304 = 0xC`,
  `0x308 = 0x10004`). The foliage lost its cut-out; reading that garbage as "on" fixed the leaves by accident and cut the
  characters. The fix: only subchannels 0 and 1 (the same as `rsx_process_method`) and the driver methods reach the
  engine; the registers keep the game's values (enable 0/1, function 0x204). Diagnostics: `RSX_ENGINE_PS_DEBUG=5` (the
  output's alpha in grey), `RSX_ENGINE_STATE_LOG=<pipeline>` (raw registers).
- PPU registers in local variables (`tools/ppu_regcache.py`, run by the lift after the translation): the generated code
  accessed `ctx->gpr[N]`, `ctx->cr` and `ctx->xer` in memory at every instruction, because the context escapes to every
  call. The pass puts those registers in locals in each function, writes them back before any line that passes `ctx` on
  (only the ones that may differ; r14..r31 stay kept across calls to functions that follow the ABI) and rereads them
  after. 131 functions write r14..r31 without restoring them (register save/restore routines and the like): a direct
  call to one of them writes and rereads everything. Functions with setjmp stay as they are. Level 1, 144 mode, in play
  only, with PGO redone: about 91 to about 97-99 fps; 10 min of combat in level 3 without a failure; the 60 mode at 60.
- Patches: guest memory stores without a stack frame, indirect calls with a short path without diagnostics, the
  profiler's stamp on HLE/syscall calls without a binary search (89 to about 91-94).
- `pgo.sh`: the profile build did not turn the Vulkan backend on (it failed to link with the PC page) and the final
  build did not recompile with the new profile (the `.profdata` is not a ninja dependency); now it passes
  `PS3RECOMP_RSX_VULKAN` and touches the generated sources. It also deletes the right test save (`NPUA80856`).
- A patch: PPU stores test watch, null scan and range at once and store; the rest went to out-of-line `_slow` halves
  (about +1 fps; `vm_write32` is about 15 % of the main thread, spread over hundreds of the game's functions).
- A fix: on a clock re-init, `+0x74` (the tick cap) stayed scaled; now the original value is read only once (before it
  went from 19 to 91).
- Tests: `pkill` without `-9` does not kill KZ1HD (it ignores SIGTERM); old instances and old scripts fight over the pad
  file and kill the new run.
- A patch: lean game logs. The `[spurs-pm] poll` line came out once per policy-module run (2 million lines, 100 MB per
  session) and `SetPortSetting` every frame; `play.sh` now uses `PS3_VERBOSE=0` and `KZ_STATS=1` (one line every 2 s
  with flips and heap). An 8-minute session: about 110 KB of log. Memory: +6 MB in the first 5 min, then stable.
- `KZ_MEMSNAP=<dir>` (`main.cpp`, reverse engineering): when `<dir>/req` appears, the guest RAM (0-0x10000000 and
  0x40000000-0x50000000) goes to `<dir>/snapN.bin`.
- Title id: the runtime kept the placeholder `BLES00000`, and the save went into that folder. `main.cpp` now reads
  `PARAM.SFO` (`NPUA80856`, as on the PS3).
- The level-load hang (a lost signal): 0 in 22 loads (the 11 levels, twice) with `jm_post` run on the PC. Closed until it
  shows again.
- `KZ_PROF_STACK=1` (`prof.cpp`): the call stack on each CPU sample, to see the game functions' inclusive time. On the
  main thread: main loop `func_0021AB28` about 81 %, render `func_0017FB14` about 43 % (`func_00054FFC` about 38 %,
  called from several places), `func_0021C0F8` (the tick wait) about 12 %.
- The 240 fps stretches at the end of some benchmark runs are the "FAILED / Retry" screen: the player, held still by the
  script, dies. The game's behaviour; the benchmark only counts the first 25 gameplay points. Run-to-run variation is
  now about ±3 fps: compare with 2+ runs.
- Dropped again: clearing colour/depth inside the batch instead of waiting for the GPU at each clear (most of the about
  22 waits per frame): 57.9 against 60.3 fps on the mean of runs.

### Above 30 fps: the game clock at 60 Hz (2026-10-16)

- The game clock is the object pointed to by `0x828734` (`0x4013F300` in Level 1): `+0x54` seconds per tick (1/30),
  `+0x58` time scale, `+0x64` frame counter, `+0x6C`/`+0x70` current/previous ticks, `+0x68` this frame's ticks, `+0x74`
  the cap of ticks per frame (4). `func_002199A8` turns ticks × seconds per tick into the time step in seconds the
  systems get.
- `KZ_TICK_HZ=<n>` (`main.cpp`): changes the double at `0x21C0F0` (ticks per ms) and `+0x54` together, and scales the cap
  `+0x74`; a tick still stands for the same game time in seconds. 30 is the original.
- `KZ_GUEST_VBLANK_HZ=<hz>`: the vblank interrupt the game sees, apart from the flip cadence (`KZ_VBLANK_HZ`). Part of the
  logic counts vblanks, so a 144 Hz screen must not hand the game 144 per second.
- `PS3RECOMP_VK_DUMP_MS=<ms>`: a frame dump every <ms> of real time, named by tenths of a second since the first frame,
  to compare runs with different fps by time.
- Validation with scripted input, frames compared at the same time: the tutorial hints' duration (about 3 s), turning
  and walking with the stick, the M82's rate of fire (the magazine emptied in the same time) the same at 30, 60 and
  120 Hz. When the enemies hit the standing player varies between runs (AI) with no clear trend.
- `play.sh` then used `KZ_TICK_HZ=60`, `KZ_VBLANK_HZ=144`, `KZ_GUEST_VBLANK_HZ=59.94`: Level 1 in real time about 41 to
  44 fps (CPU-limited), menus at 144. (Since then: a steady 60 by default, 144 as an option; see
  [performance.md](performance.md).)

## Since 2026-10-17

The journal continues in topic notes: [performance.md](performance.md) (the 144 Hz work, to 2026-10-27),
[ps3recomp-v018-migration.md](ps3recomp-v018-migration.md) (2026-10-27/28), [windows-build.md](windows-build.md)
(2026-10-28/29), [wws-jm-post.md](wws-jm-post.md) (the post-processing chain), and the [roadmap](../ROADMAP.md) for the
PC features added since (widescreen, FSR 1, presets, 5.1/7.1, languages, trophies).
