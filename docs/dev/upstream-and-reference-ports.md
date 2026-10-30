# ps3recomp upstream and the reference ports

A survey made on 2026-10-27 and extended on 2026-10-28/29: what changed in [ps3recomp](https://github.com/sp00nznet/ps3recomp)
since our base of the time, which issues and pull requests matter to us, what each reference port does that helps
us, and a plan in order of priority. Patch numbers are the private series of each date (before 2026-10-28, the
v0.14.0 series; [patch-numbers.md](patch-numbers.md) maps the v0.18.0 one).

Items marked **(to measure)** are hypotheses: they need an alternating A/B before any conclusion.

## Summary

1. **Upstream was already at v0.18.0** (2026-10-26), not v0.15.0: 785 commits and 505 files since v0.14.0, in four
   releases: v0.15.0 (76 PRs), v0.16.0 (44), v0.17.0 (33) and v0.18.0 (155).
2. **Part of our performance work already existed upstream**, written by other ports: inline memory stores with a
   cheaper path (#573, #574), an atomic lwmutex with a shortcut for leaf HLE (#577), per-attribute vertex fetch (#458),
   draw batches in Vulkan (#532), command-ring recycling (#553). Moving up made several of our patches unnecessary or
   in need of measuring against upstream's.
3. **The move had a real cost:** a test merge (`git merge-tree`, without touching the repository) conflicted in 27
   files, about 128 hunks, mostly in the Vulkan backend (31), the draw engine (19), the PPU lifter (13), `cellGcmSys`
   (12) and `cellPad` (10).
4. **The biggest new performance lead for the main thread** came from the inFAMOUS 2 port: a `__restrict` context
   pointer in the translated code, stores without a compiler barrier and
   `-mllvm -dom-tree-reachability-max-bbs-to-explore=1000`. clang then keeps the PPU registers in host registers across
   memory accesses. That may also explain why our inline stores everywhere gave −3 % **(to measure)**.
5. **undecuple (Killzone 2 and Killzone 3 ports) was already on v0.18.0.** Of their 62 fixes, 18 were not upstream.
   Some match ours: `--overrides` with `__lifted` is our `PPU_LIFT_NATIVE`, and their two frames in flight are our
   0094. Others cover roadmap items: trophies from the TRP and a safe offline network policy.
6. **KillzoneRecomp is the PS2 Killzone** (through PS2Recomp, not ps3recomp). Its runtime is of no use to us, but the
   game is the same. Its author measured logic bugs that only appear above 30 fps: the stick's turn acceleration counts
   frames, and the camera lag is an Euler step. Killzone HD probably has the same bugs at 60 and 144 Hz **(to check in
   the binary)**.

**Decided on 2026-10-27:** move to **v0.18.0**, re-applying our patches one by one, **before the Windows build and
before going back to performance**. Done on 2026-10-28 ([ps3recomp-v018-migration.md](ps3recomp-v018-migration.md)).

## Our base at the time

- ps3recomp v0.14.0 plus 27 PRs merged by hand: #253 to #258, #266, #268 to #273, #275 to #282, #285 to #289 and #297.
  All of them later went into v0.15.0.
- On top, our 108 patches (149 commits, 129 files, +15,381/−984 lines).
- Left out of v0.15.0 because of conflicts: #290 and #291 (per-context MFC/reservation registers and policy-based SPURS
  scheduling), #283, #284, #295, #296 and #243.

## The upstream delta: what to adopt

### What upstream already solves its own way (candidates to drop our patch)

| Our patch | Upstream | Action |
|---|---|---|
| 0001 jump table based on the stack | #444 ("spilled" PPU jump tables) and #311 | Check it covers our case and drop ours |
| 0004 strict flips, 0048 flip queue | #343 (a flip completes when the RSX reaches it), #354 | Test upstream's; keep ours only if 144 Hz pacing gets worse |
| 0007 250 ms grace on a JUMP to itself | #312, #531 (`GCM_PARK_GRACE_MS`), #541 | Drop ours |
| 0013, 0039 a draw that reads its own target | #352 (colour feedback snapshots in Vulkan) | Drop ours if the host scenes and Level 1 pass |
| 0026 engine draws in one command buffer | #532 (`VK_DRAW_BATCH=1`), #552 | Measure both; keep the faster |
| 0034, 0083, 0091 to 0093, `inline_hot_stores.py` | #573 and #574 (inline stores with a copy of the memory model, one compare and a branch hint), #534 (inline reserved-line bitmap), #483, #528 | **Measure carefully:** upstream inlines everywhere, which gave −3 % here. Measure both in Level 7 |
| 0040 linear A8R8G8B8 per row | #432 (one rotation per texel) | Drop ours |
| 0042, 0065, 0107 texture re-hash | #375 (sparse re-hash), #439 (no hash for textures the engine rewrote), #438 | Measure; our range hash may still pay |
| 0045, 0050 SPIR-V and pipeline caches on disk | #346 (cached HLSL→SPIR-V translations) | Check whether upstream writes to disk; if not, ours stays |
| 0063 atomic taskset bitsets | #340, #388 | Drop ours |
| 0076 lean logs | #399, #431, #521, #524 (buffered stdout) | Keep only what is still missing |
| 0095 per-attribute vertex fetch | #458 (same idea) | Drop ours if the A/B ties |
| 0106 the game's ring recycling | #553 (recycling of the ring itself counts as a JUMP to the start) | Test; ours was made for PSGL's ring |
| 0108 HLE shortcut, line-lock wait | #577 (lwmutex with an atomic word and a leaf-HLE shortcut), #443 (a short spin before sleeping) | Measure both in Level 7 |
| 0015, 0022, 0025 SPU floats in AVX2 | #369, #370, #479 (`SPU_FLOAT_RZ`), #500, #512 | Measure; probably drop ours |
| 0028 lighter SPU dispatch | #481 (per-thread dispatch cache, on by default), #436, #421 | Measure |

### New fixes that matter to us

- **Vulkan and RSX:** per-target colour mask and blend in MRT (#516); user clip planes (#537); polygon offset (#562);
  `CLEAR_SURFACE` with scissor (#560); depth read as a texture converted on the GPU (#499, #502, #507); shadow compare in
  the right order (#424); WPOS in antialiased targets (#446, #589); 28-bit vertex address (#504); CMP 11-11-10
  attributes (#529). Several may be behind visual differences not yet seen in levels 2 to 11.
- **Frame pacing:** a flip checked when the RSX reaches it (#343); a late completion that does not mark a newer flip
  (#354); GCM callbacks in `sys_timer_usleep` (#353).
- **Threads and sync:** a thread id not reused right after exit (#415); non-joinable threads free their slot (#409);
  the main thread's stack info (#469); each host thread runs HLE→guest callbacks on its own stack (#489).
- **Files and saves:** fixed `CELL_FS_O_*` flags (#349); the file position goes back when the fd switches between
  reading and writing (#456); `PS3_GAMEDATA_ROOT` (#338), which can replace our separate test run folder.
- **PC and platforms:** `PS3_LANG` picks the system language (#333); `ps3_host_quit` lets the close button reach the
  game (#544); the template maps the whole 32-bit guest space on POSIX (#475); the runtime builds with MSVC (#588);
  MinGW builds the tests (#402); XInput loaded at run time (#494); WASAPI with 60 ms queued (#514); 5.1 and 44.1 kHz in
  WASAPI (#385).
- **Diagnostics:** `PS3_FTRACE` (#339), the timeline of who waits for whom; `PS3_TPROF` (#480); `RSX_WALKER_PROF`
  (#467); `PS3_SAMPLE_DRAIN` (#520, #568); an ftrace span per HLE call (#580). Every `getenv` left the hot paths
  (#437, #519, #527).
- **Tools:** `ppu_lifter --insn-patches` translates patched instructions as run-time switches (#539), which could
  replace some of our hooks in `main.cpp`. `lift_jobmods` lifts each module at the base its header names (#488).

### What upstream still lacks and we need

- **Vulkan on Windows:** v0.18.0's `CMakeLists.txt` still refuses `PS3RECOMP_RSX_VULKAN` outside Linux. It was the
  biggest block for our Windows build, and is our best contribution to upstream (also asked for in #183).
- **Two frames in flight in Vulkan** (our 0094 of the time, +20 %): upstream still waits for the frame's batch.
- **Internal scale, display modes and widescreen in Vulkan:** upstream has only proposals (#546 for Metal, #614 for a
  resizable window).

### A known v0.18.0 problem

You Don't Know Jack sometimes hangs at boot on a SPURS `EventFlagWait` no workload signals (4 of 4 runs in the
regression gate, 1 of 4 on v0.17.0). Killzone HD uses SPURS heavily, so this belongs in our checks after the move.

## Cost of the move

`git merge-tree --write-tree <our branch> origin/master` (computes only, changes nothing):

| File | Conflicting hunks |
|---|---|
| `libs/video/rsx_vulkan_backend.c` | 31 |
| `libs/video/rsx_draw_engine.c` | 19 |
| `tools/ppu_lifter.py` | 13 |
| `libs/video/cellGcmSys.c` | 12 |
| `libs/input/cellPad.c` | 10 |
| `runtime/spu/spu_context.h`, `spu_channels.c` | 6 each |
| `runtime/spu/spu_coherency.c`, `libs/spurs/cellSpurs.c` | 5 each |
| 18 other files | 1 to 4 each |

Two ways:

- **Re-apply the patches** on a new branch from v0.18.0, one by one. The ones upstream covers stay out, and each
  remaining patch is still a clean file in `port/patches/`. More work at first, but the stack gets smaller and easier
  to send upstream.
- **Merge** `origin/master` into our branch. Solves everything at once, but mixes our patches with upstream's and makes
  the next move harder.

The first was chosen. SPURS was the riskiest part: #291 replaced the scheduler with a coordinator plus persistent
workers, and our lane patches were made for the old scheduler.

## Open issues and pull requests that matter

| # | What it is | Why it matters |
|---|---|---|
| [#214](https://github.com/sp00nznet/ps3recomp/issues/214) | The 40 (later 62) fixes of undecuple's Killzone 3 port | 26 went in. Still to send: the live-draw halves of V8/V9, R3, R4, G6, V6, V7, L4 and P3 (`--overrides` for functions replaced by the host, our extension point for mods). Their port builds for Windows with MinGW-w64 on Linux and runs under Proton, the same route we took |
| [#447](https://github.com/sp00nznet/ps3recomp/pull/447) | The Folklore port's line: draw engine on Vulkan for Windows, Linux and Android; integer internal scale; FSR 1; CACAO; DLSS SR with motion vectors; a bridge to FSR 3.1 | Exactly what phases 4, 7 and 8 need. Held back over licensing in 5 files (GPL-2.0); read the design, do not copy the code |
| [#183](https://github.com/sp00nznet/ps3recomp/issues/183) | Prefer Vulkan over D3D12 | The maintainer wants Vulkan on Windows once the backend matures. A PR of ours with a `LoadLibrary` loader fits here |
| [#546](https://github.com/sp00nznet/ps3recomp/issues/546) | Internal scale, texture packs by content hash (`PS3_MODS`), grading at present | Texture packs are phase 12; the content-hash design handles textures that move in memory |
| [#603](https://github.com/sp00nznet/ps3recomp/issues/603) | The translated code as a separate library, with an ABI check | Fits the "bring your own game" launcher (phase 11): the runner ships ready and only the game's module is compiled on the player's machine |
| [#614](https://github.com/sp00nznet/ps3recomp/pull/614) | A resizable window that keeps the aspect, `PS3RECOMP_FULLSCREEN` | Overlaps our window patches; check before sending ours |
| [#612](https://github.com/sp00nznet/ps3recomp/pull/612) | Keyboard as a pad on port 0 with SDL | Overlaps our keyboard patch |
| [#566](https://github.com/sp00nznet/ps3recomp/pull/566) | `GCM_FIFO_SEGMENT`: the command ring delivered in segments | The same area as our ring recycling |
| [#345](https://github.com/sp00nznet/ps3recomp/issues/345), [#337](https://github.com/sp00nznet/ps3recomp/issues/337) | How inFAMOUS 2 (Metal) went from 7 to 26 fps | Method: look at the frame as a timeline (`PS3_FTRACE`), not only as a profile; move to the GPU the SPU jobs the RSX waits for |
| [#497](https://github.com/sp00nznet/ps3recomp/issues/497) | SECUREFILE saves written in the clear | Check whether Killzone HD uses SECUREFILE before promising console-compatible saves |
| #619 to #625 (MaxLeiter) | lv2 syscall fixes: SPU groups, events, locks with timeouts, `sys_fs`, join on Windows | Semantics fixes that reduce rare hangs |

## Reference ports

### KillzoneRecomp (drakolordx7): the PS2 Killzone, Windows

[github.com/drakolordx7/KillzoneRecomp](https://github.com/drakolordx7/KillzoneRecomp), GPL-3.0, active (last commit
2026-10-26). The original PS2 Killzone (SCUS-97402 and SCES-52893), translated with PS2Recomp, with PCSX2's GS, microVU,
IPU and SPU2 linked as libraries. Windows only. **It does not use ps3recomp**, so its runtime is of no use; its value is
the game, which is Killzone HD's base. Addresses do not match ours, but the logic and property names should. Read for
behaviour only (GPL): no code taken.

What helps us (its `docs/findings.md`, 1,523 lines of measured reverse engineering):

- **Speed parity against the original.** They run PCSX2 hidden, inject the same pad script into both and compare
  per-frame CSVs (walk, strafe, sprint and turn speed, rate of fire, reload, weapon switch, crouch) at 30, 60, 120, 144
  and 240 Hz. Our equivalent would be RPCS3 with the same `PAD_SCRIPT`.
- **Logic bugs above 30 fps they found and fixed:**
  1. The stick's turn acceleration adds up per frame (a counter at `ctrl+0xBC`, ramping from 1x to 2x after 15°). At
     120 Hz the ramp started at 0.1 s instead of 0.27 s, and at half stick it showed where the original is flat.
  2. The camera's lag behind the aim is an Euler step (`lag = (lag − step) × 10 × (0.1 − dt)`), exact for one dt only.
  3. Rate-of-fire, weapon-switch and crouch counters are quantised in frames: at 240 Hz the rifle fires 7 % faster.

  Our check of 2026-10-16 covered hints, movement, turning and rate of fire at 30, 60 and 120 Hz, but with the mouse.
  **Turning with a controller stick at 60 and 144 Hz was not measured.**
- **Mouse aim:** each turn value acts twice (in its frame and the next), in the original too; their fix is a turn
  "debt" spread over two frames. Hold to aim (R3 on keyboard and mouse only), sprint held as in the original, with a
  toggle option.
- **The game's post-processing mapped:** glow, colour grading, motion blur (blend with the previous frame), grain and
  the rain lens blur, with the `PostProcessPreset` object and its fields. No depth of field on the PS2. The map for
  phase 8's "post-processing toggles".
- **View distance and LOD:** `RenderZone` (far plane, fog) and the FOV-based LOD factor, with hooks that scale the plane
  and fog together.
- **Input latency measured** (15 to 22 ms at 120 Hz) and the frame queue limited to 1.
- **Product:** a tabbed launcher (controls, ultrawide, skip intro), Kenney's keyboard and mouse icons (CC0) switched
  live, `killzone.log` plus `killzone.prev.log` for reports, one executable for both regions, a multi-ISA build (SSE4.1,
  AVX and AVX2 picked by CPUID) and a clear message when AVX2 is missing.

### inFAMOUSreborn (whompay): inFAMOUS, Windows

[github.com/whompay/inFAMOUSreborn](https://github.com/whompay/inFAMOUSreborn), D3D12, ps3recomp embedded with its own
changes.

- **A ready "bring your own game" installer, in Rust with no dependencies.** It reads `PARAM.SFO`, prepares the player's
  files with the player's own emulator install, checks SHA-256 hashes, extracts the ISO, creates a venv, runs the
  translation, checks every generated file against a `generated.lock`, applies the patches, compiles with clang-cl and
  creates shortcuts. Updates download only the changed files (git blob hashes) and recompile only the affected units.
  The most complete design there is for our phase 11.
- **DLSS, DLAA, FSR 3.1/4 and FSR frame generation on D3D12.** Halton jitter applied to the 3D scene's draw viewport.
  Motion vectors from camera reprojection of depth, with a zero-motion fallback for what moves with the camera. The HUD
  goes on top, scaled. The SDKs stay out of the download; the player fetches them and the build finds them. The model
  for phase 8's motion vectors, jitter, FSR 3.1, DLSS and FSR frame generation, to port to Vulkan.
- An options menu in the pause, a low-CPU mode, a compatibility build without AVX2, packaged PGO, native DualSense and a
  trainer (F10).

### infamous2-pc (ThiagoFraga761): inFAMOUS 2, Linux and Vulkan

[github.com/ThiagoFraga761/infamous2-pc](https://github.com/ThiagoFraga761/infamous2-pc), MIT. ps3recomp v0.13.0 with a
single patch. The closest to us: Linux, Vulkan, resolution scale, uncapped fps and research documented in `rpi/` and
`docs/NOTES.md`.

- **PPU code compiler flags** (`work/port/tools/ppu_cxx.sh`): `clang++` for the translated files only, a `__restrict`
  context, inline stores as `volatile` accesses instead of a compiler barrier, and
  `-mllvm -dom-tree-reachability-max-bbs-to-explore=1000`. They report their pier scene going from 98 to about 119 fps
  with the main thread at 95 %. We already compile with `clang++`, but the lifter emits no `__restrict` and we do not
  pass that LLVM limit. The most promising lead for the main thread's missing 10 % **(to measure)**.
- **Other main-thread gains:** stack (r1) accesses without checks (we have something similar); indirect calls straight
  from the hash to the function; short `usleep` spinning with PAUSE; `vperm` as two shuffles, `vsldoi` as one
  `palignr`, `lvebx/lvehx/lvewx` as a 16-byte load (avoids store-forwarding stalls); and logging off
  (`PS3_VERBOSE=0`), which alone cost 14 %.
- **Video-memory write tracking by page protection** (`PS3RECOMP_WTRACK`, `runtime/memory/guest_wtrack.c`): pages read
  are made read-only and the first write is stamped. Texture lookup from 3.7 to 0.3 ms per frame, and a geometry cache
  validated by the same tracking. It is "path 3" of our [performance.md](performance.md) (the texture hash, about 13 %
  of the RSX thread).
- **Vulkan command recording on its own thread** (`PS3RECOMP_BE_THREAD`, +12 % in the heavy scene), vertex pulling in
  the shader, geometry resident in VRAM, deferred object destruction (removed about 5,800 waits per second when turning
  the camera), resolution scale done in the backend with the engine seeing guest sizes.
- **Menu and launcher with ImGui** (SDL2 + Vulkan), F1 in game, separate voice and subtitle languages
  (`PS3_SYS_LANG`).
- **SPU jobs rewritten on the host or the GPU** (Edge Geometry, culling, animation, SSAO and lighting on the GPU),
  each with research in `rpi/`. For us the method counts, since Killzone HD is limited by the PPU.

### allstars-port (samuelMadureira23): PlayStation All-Stars, Windows

[github.com/samuelMadureira23/allstars-port](https://github.com/samuelMadureira23/allstars-port), MIT, D3D12.

- **A "bring your own game" kit** and a Python/Tk launcher with guided steps (check the game, prepare the files, check
  the ELF, tools, generate, compile). Supports the 1.12 update and DLC over 1.00, which could serve Killzone 2 and 3,
  which had patches.
- **Shader precompilation before play**, with a progress bar: the answer to first-compile stutter, which every port
  reports.
- **Proof that the executable is native** (`tools/prove_native.py`) and `difftest`, difference tests against a
  reference.
- LAN matches with the game's own netcode, protocol documented. A reference for phase 10.

### tokisaki-port (YYOzcan): Killzone 2, Linux and Vulkan

[github.com/YYOzcan/tokisaki-port](https://github.com/YYOzcan/tokisaki-port), ps3recomp v0.15.0 as a submodule and 38
patches of its own. Pre-alpha: the first level plays at about 13 to 14 fps on a laptop.

- **The direct peer for phase 9.** The same WWS and `jm_post` as ours, Edge Zlib inflated on the host like our FIOS,
  entity updates in fibers.
- Patches that may help us: a Vulkan texture pool reusing image, memory and view (creating about 45 textures per frame
  was 20 % of the FIFO thread on NVIDIA); constant UBOs with a dynamic offset and one descriptor set per texture set
  (half the descriptor time); direct mouse (1 count = 0.022° × sensitivity, no stick curve); MAILBOX on NVIDIA; window
  events pumped during long drains (like ours); PGO of the SPU code.

### undecuple's Killzone 2 and Killzone 3 ports (before their repositories were public)

Sources: their 58 issues and PRs in ps3recomp (2 open issues, #214 and #497; 54 merged PRs, #215 to #535) and the
[gist of 62 fixes](https://gist.github.com/undecuple/2845432bf13e485634de122084d3fd56), revised on 2026-10-26. At the
time no port repository was public; Killzone 2 appears in #320 ("found by our Killzone 2 port": a carry register reloaded
with the input value froze the AI's waypoint search).

State then: a Windows build with MinGW-w64 GCC on Linux, run under Proton and vkd3d-proton (a D3D12 "live-draw"
backend). Killzone 3's nine levels loaded, one played to its end. **The gist was already on v0.18.0**: of 62 fixes, 30
went in, 10 went in partly, 18 were theirs only and 2 no longer applied. Always one PR per fix, with a test that fails
before and passes after.

**What it meant for us:**

- **They had already moved to v0.18.0.** The only version where a common base for the three games is possible without
  another move.
- **P3, `--overrides`:** the lifter emits `func_<addr>__lifted` and announces `PPU_OVERRIDE_<addr>`; the port defines its
  own `func_<addr>` and may call the translated body. The same mechanism as our `PPU_LIFT_NATIVE` (`<name>_lifted`,
  `native_ppu/`). Upstream has only `--weak`, which replaces but cannot call the original. Worth one PR together rather
  than two similar mechanisms; it is also the base of phase 12's mod API.
- **V14, two frames in flight** in D3D12 (from 100-109 to 112-122 fps in a menu): the same idea as our Vulkan patch.
- **MinGW and emulated TLS** (P5, P9, S9): with MinGW GCC thread-locals are emulated and costly on hot paths. Our
  out-of-line `vm_read*` has such detectors too (`g_last_rd_addr`, `PPU_THREAD_LOCAL`). Hence our Windows build uses
  clang (native TLS).
- **S8, a line lock in 64 stripes with a seqlock in `GETLLAR`:** 16 to 18 % of acquisitions contended the global lock in
  a Killzone 3 level, falling to 0.24 to 0.36 %. Upstream chose another design (#441, #443, #484); measure theirs
  before proposing stripes.
- **L7, trophies from the TRP** (`TROP.SFM`): names, grades, automatic platinum, per-user storage. Covers our phase 6
  item "trophies as a local log" (done, after Project Pyrrhus's 5004).
- **N1 to N4, an offline network policy:** one online switch, a name map, an allow list and local answers for the DNS
  the game itself sends over UDP. The base for community servers in phase 10 without leaking traffic to Sony's closed
  servers.
- **G6, NV0039 copies** (`cellGcmSetTransferData`) on subchannel 1: they were dropped and their methods became 3D
  registers, corrupting state. The MAG port (tekulvw, M092) found the same bug.
- **V11, conditional rendering and occlusion queries:** without them, hidden coronas and light volumes are drawn anyway
  (523 draws per flip in a Killzone 3 level). Only in their D3D12; for us it would be Vulkan work in phase 9.
- **Also useful for phase 9:** Killzone 3 uses MLAA on the SPUs, which they replaced with a host FXAA; Killzone 3's
  movies are Bink decoded by the game itself; and there is the
  [MAG port's gist](https://gist.github.com/tekulvw/c925f1e3267e504feb614d58fc0214b9) (94 fixes) in the same format.

### Project Helghast: undecuple's Killzone 3 repository (public since 2026-10-28)

[undecuple/project-helghast](https://github.com/undecuple/project-helghast), MIT, version 0.1.4 when read in full on
2026-10-28 (pre-release, "partly playable", 16 to 22 fps in the levels, target 30): its 266 patches (every description,
the code of those that matter), the docs, the port code (`port/kz3`, `port/spu`, `port/vfs`, `port/net`), the tools and
the installer. The "RE notes" its patches cite are not in the repository.

Layout: ps3recomp as a submodule, the patches applied to a clean copy before each translation and build, already
rebased on v0.18.0. Numbered by area: 0xxx lifter, 1xxx SPU/SPURS, 2xxx graphics (D3D12 "live-draw") and PC, 3xxx lifter
and libraries, 4xxx network and input, 5xxx data, update and trophies. Each runtime fix has a host test that fails
without it, and CI checks that every patch applies. This repository follows the same layout.

**What to take, by priority:**

1. **The SPURS kernel and the WWS job manager (high; tied to the 144 Hz weapon problem).**
   - **1000-spurs reverts the whole #291**, the v0.18.0 scheduler we adopted.
   - **1074:** one thread per SPU of the instance, with the real kernel's selection rule, from NoahBPeterson's PR #213. A
     workload only runs if it is *wanted*: a signal, a flag, or `readyCount != 0` and
     `readyCount + idleSpuCount > contention`. The `ReadyCount` imports return the instance byte's old value, because
     the WWS policy module zeroes its own ready count when the list empties and gives it back with a CAS. And
     `cellSpursRequestIdleSpu` is no longer a stub. (Taken: our 1017.)
   - **1058:** the `selectWorkload` interception answers the current workload, so the job manager runs its jobs in a row
     instead of one job per kernel pass. (Taken: our 1018, the fix for the weapon parts.)
   - **1045:** `ReadyCountSwap`/`CompareAndSwap` also write the instance byte. **1064:** lanes by ready count. **1037:**
     the kernel stays awake a few µs after a busy pass.
   - **1062:** `_cellSpursAttributeInitialize` takes `(attr, revision, sdkVersion, nSpus, …)`. The old order gave 2/2
     SPUs in Killzone 3 instead of the real 5/1. Our log shows HiPrio with 4 and LoPrio with 1: **check that our reading
     of the arguments is right.**
   - **1076:** a probe of the job manager's list, with the same header layout we found in `jm_post`.
2. **Windows build (high at the time).** The same route we planned: MinGW-w64 cross builds on Linux, tests under Proton,
   and it runs on native Windows 10 and 11. Releases ship **llvm-mingw** (clang, with native TLS). Patches read before
   starting: 0002 (the PPU loader on MinGW), 1112 (msvcrt `timespec_get`), 1113 (runtime memory windows), 1130
   (`g_spu_coh_bits` with a single definition for MinGW's ld); **1099** (translated SPU functions use the System V
   convention on Win64); 1115 (a TEB slot for the trampoline), 1116 (`setjmp`/`longjmp` with host frames),
   1017/1020/1127 (emulated TLS and `getenv` out of hot paths), 1063 (profiler without the loader lock under Wine); in
   `main.cpp`, the 4 GB guest memory reserved and committed on demand by an exception handler (VEH).
3. **Distribution (high, right after the Windows build).** `tools/release`: an Inno Setup installer and a portable zip
   that translate and compile the game on the player's machine, with the compiler pinned by SHA-256
   (`toolchain.json`, `fetch_toolchain.py`); `firstrun.py`: the build steps with a progress bar, jobs by free memory and
   a low-memory mode; EBOOT and disc archive checks by SHA-256, an ISO reader; **RPCS3 import** (`rpcs3_import.py`,
   `rpcs3_locate.py`): finds the game, the update and the saves in an RPCS3 install, the natural way for our players to
   point at Killzone HD's package; `shaders.py`: a shader warm-up session right after the build; opt-in crash reports
   with personal data removed, `portable.txt`, refusing OneDrive folders, a launcher in C; `docs/FORKING.md`.
4. **Shaders without stutter (medium).** Asynchronous pipeline builds (2048 and 2058), the list of used pipelines saved
   and pre-created next session (2031), a pipeline template shipped with the game, with only hashes and numeric state
   (2071, 2077), and a key stable across sessions (2042). Our Vulkan cache keeps SPIR-V but creates pipelines on demand.
5. **Runtime fixes that may apply to us (medium; check each against v0.18.0):** 1111 (raw PPU stores such as `stvx`
   respect SPU reservations), 1122 (handoff semantics of `sys_mutex`/`lwmutex`/`lwcond`), 1059 (a FIFO queue per
   `lwcond`), 1095 (FTZ/DAZ on SPU threads; we have it through MXCSR), 1136 (SPU threads on P-cores of hybrid CPUs),
   1065 (the "repeated DMA" lock counted per context), 2104 (the FIFO walker takes the new JUMP, `offset | 1`).
6. **Tests and diagnostics (medium):** `PAD_SCRIPT` v2 (3006: hold by event, stick and trigger segments, a file with
   comments); a frame-pacing metric (2072: a histogram of present intervals); per-thread wait accounting (1032) and a
   per-frame event trace (1085).
7. **Input and menus (low, we have much of it):** new pages inside the game's own menus (GUM files served by the VFS:
   Keyboard, Mouse, Display, "Quit to desktop"), mouse in the game's menus by hit-testing widgets (2098), a per-action
   layout editor (4014, 4022, 4023), keys by physical position (2061), per-device icons (2090, 2094), the HUD kept 16:9
   with fixed marker projection (2113, 2115), FOV, bloom and motion blur in percent (2109, 2121).

**A correction:** render-time camera interpolation (2114) is **off** in their port, because measured it was worse: the
pose it interpolated already follows frame time, and the blend added error. What stays on is mouse look with "late
latch" (2108, 2112, 2116): the camera rotation read as late as possible, near present. For us, an idea for the mouse
only, not a ready path past 60 fps.

**Not directly useful:** the D3D12 backend (the ideas of GPU vertices and a resident vertex cache, 2053/2076/2092, and
batched rewrites, 2018/2119, are references), the network/NP layer and Discord (phase 10 at most), and Killzone 3's SPU
job hooks (culling, Bink, audio), which are addresses of their game.

### Project Pyrrhus: undecuple's Killzone 2 repository (read 2026-10-29)

[undecuple/project-pyrrhus](https://github.com/undecuple/project-pyrrhus), MIT, version 0.1.2 (pre-release, "partly
playable") when read on 2026-10-29: the README, the docs, the CHANGELOG, `AGENTS.md`, the 220 patches (every header and
the code of those that matter), the port code (`port/kz2`), the `re/` tables and the tool and installer lists.

**Their state:** Windows x64 with Direct3D 12, tested under Proton; the installer also ran in a Windows 10 VM, without
play. The target is the US disc (BCUS98116) with the 1.29 update (about 54,000 PPU functions). The opening movie, the
menus with sound and the first level (Corinth River) work, played with a DualShock 4 at about 23 fps in a scripted run;
the PS3 runs at 30. For Vulkan, their README points to tokisaki-port.

**Important for us:** their installer accepts the **Killzone Trilogy disc**, whose `PS3_GAME` is Killzone 2 with the
same BCUS98116 (disc version 02.00, "accepted, untested").

**Layout:** the same as Project Helghast, plus two blocks: `6xxx` (fixes found bringing Killzone 2 up) and `7xxx` (SPU
and SPURS). About 150 of the 220 patches come from Killzone 3 as they are. PPU function overrides by address
(`ppu_overrides.txt`, one list per EBOOT), probes in the middle of functions (`--probes`, 6017) and host fibers for
guest context switches (`guest_fiber`, from Killzone 3). `AGENTS.md` holds the rules for AI agents: nothing of the game
in git, no GPL code (RPCS3 read only), `ENHANCEMENT:` and `ASSUMPTION:` marked, network only by the player's choice.

**What to take, by priority:**

1. **Local trophies (high; done as our 5001).** Patch **5004**: `sceNpTrophy` reads the game's own `TROPHY.TRP` (ids,
   grades, hidden, platinum and names from `TROP.SFM`), unlocks the platinum when all linked trophies are unlocked,
   stores `TROPUSR.json` atomically and tells the host about each trophy, for an on-screen notice.
2. **Runtime fixes that may apply to us (medium):**
   - **6024**: `cellAudio` consumes blocks on a fixed 5.333 ms clock, not when the device asks. Under Proton (winepulse)
     the device asked for two blocks at once, and the game's mixer overwrote one (crackles in movies). Worth testing our
     sound under Wine/Proton and on Windows.
   - **6026**: the FIFO takes the new JUMP (`offset | 1`), with a test.
   - **6027** (from the Ratchet & Clank port): lv2 mutex semantics, with an owner per guest thread, handoff on unlock and
     on cond signal, and destroy rules. **6005**: `lwcond` with a FIFO queue of waiters. **6019**: the walker wakes
     whoever waits on a label instead of polling with `usleep`.
   - **7001**: `_cellSpursAttributeInitialize` reads its arguments in the SDK's order, which gave 5/1 SPUs in Killzone 2
     instead of 2/2 and twice the frames. The same point as Killzone 3's 1062; our reading (HiPrio 4, LoPrio 1) is
     still to check.
   - **6036**: `sys_fs_open` with the console's flag values (`O_APPEND` was read as `O_EXCL`).
   - **6030**: per-event log lines printed 8 times and then only as a count. Our Windows log has about 39,000
     `[cellSpurs] _SendSignal` lines in 7 min (2 MB); `tools/run.sh` filters them, `kz1hd.log` does not.
   - **6034**: reads a psarc split into parts (from a FAT32 copy) as one file.
3. **Windows and distribution (medium):** **2124**: the window gets its own thread, because Windows marks a window that
   handles no messages for 5 s as "not responding"; **2123**: the exe's icon on the window and taskbar; **6033**:
   `g_spu_coh_bits` as `selectany` on MinGW; **7003**: `getenv` once only on hot paths (measured: not worth it for
   Killzone HD); the installer and first run, more complete than Killzone 3's: decrypted ISOs, the disc title read,
   OneDrive refused, RPCS3 import, messages that name the failing input, and `pso-template.bin` (a pipeline list
   shipped with the port).
4. **Tests (low):** **4014**: stick lines in `PAD_FILE` (`S LX LY RX RY SECONDS`); **3006**: `PAD_SCRIPT` v2.
5. **Checked and not our case:** **6012** (`lswi`/`stswi` in the lifter: our lift has none); **6031** and **7004** (async
   SPURS tasks: Killzone HD runs workloads and job managers, no SPURS tasks); **4015** (SIXAXIS motion from the stick:
   Killzone HD does not use motion); the D3D12 backend; **6013**/**6023**/**6022** (`cellVdec`/`cellAdec` decoders: our
   movies go through our own `cellSail`).

**For phase 9:** undecuple has both games, is on v0.18.0 and keeps a patch format we can follow. The shortest path to
Killzone 2 is running Project Pyrrhus with the Trilogy disc and following their decision on Vulkan, rather than
starting our own port from scratch.

## Plan in order of priority (2026-10-29)

1. ~~**Move ps3recomp to v0.18.0**~~ (done 2026-10-28).
2. **Generate the PPU code the way inFAMOUS 2 does** (`__restrict`, `volatile` stores instead of a barrier, the LLVM limit
   at 1000) and redo the A/B of inline stores across all the code. Cheap to try and aimed exactly at the main thread's
   missing 10 %.
3. **Check the logic bugs above 30 fps** KillzoneRecomp found: find Killzone HD's equivalent routines (turn
   acceleration, camera lag, counters) and measure with a controller at 30, 60 and 144 Hz against RPCS3. Fix before
   closing phase 6, because it is fidelity.
4. ~~**Windows build**~~ (done 2026-10-28/29: Wine, Proton and a real Windows, [windows-build.md](windows-build.md)).
5. **Video-memory write tracking** (the `guest_wtrack.c` model) for the RSX thread's texture hash, when it limits again.
6. **The PC options left in phases 7 and 8, with the ports as models:** post toggles with KillzoneRecomp's map; shader
   precompilation with progress (All-Stars); DLSS/FSR by jitter and camera reprojection (inFAMOUSreborn), ported to
   Vulkan.
7. **The launcher's design** (phase 11) from inFAMOUSreborn's installer (Rust, `generated.lock`, incremental update),
   Project Helghast's and Project Pyrrhus's first-run builds, and proposal #603 (the translated code as a separate
   module).
8. **Send fixes upstream, one PR per fix, with a test** (undecuple's pattern): Vulkan on Windows; two frames in flight;
   `setjmp/longjmp`; the 128-byte reservation between PPU threads; `cellAudioAddData`/`Add2chData`/`Add6chData`; the
   `cellKbRead` NID; alpha test; internal scale and window modes in Vulkan, checked first against #614. That shrinks
   our stack at every move.
9. **Talk with the Killzone 2 and Killzone 3 ports:** first topics with undecuple: one override mechanism (their P3 and
   our `PPU_LIFT_NATIVE`), two frames in flight and trophies.

### Decision: the ps3recomp version (2026-10-27)

| Option | For | Against |
|---|---|---|
| **v0.18.0, re-applying the patches (chosen)** | The same base as undecuple's ports; brings MSVC/MinGW for the Windows build and the RSX fixes; about 15 to 20 of our patches go away | More conflicts now (27 files); SPURS changed scheduler; the YDKJ problem needs checking |
| v0.15.0, as the roadmap said | A smaller step; the same base as tokisaki-port | Soon three versions behind and the next move pays the same conflicts; brings nothing that matters for Windows |
| Stay on v0.14.0 and take fixes one at a time | No risk now | The stack only grows, and our upstream PRs do not apply to master |

The upstream `CHANGELOG.md` stops at v0.15.0; notes for v0.16.0 to v0.18.0 are only in the
[releases](https://github.com/sp00nznet/ps3recomp/releases).
