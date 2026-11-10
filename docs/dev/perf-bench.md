# Benchmarking with recorded routes

How to compare two builds (or two settings) on the same scenes, and why the routes recorded by playing need a
rebase first. Measured 2026-11-09/10 on 55bd3b4 (v0.4.0, ps3recomp v0.19.0); the numbers are in the maintainer's
notes for that date.

## Why a played recording does not replay exactly

A route (`port/kz1hd/route.cpp`) replays the player's recorded input, frame by frame, from the player's first tick.
Two things make a replay leave the recorded path:

- **The settings.** The mouse look is recorded raw (`M dx dy`) and turned into a turn rate with the current
  `mouse_invert`, `mouse_yaw` and `mouse_pitch` (`pcoptions.cpp`), and `field_of_view` changes what the camera and the
  AI see. A route replayed with another `kz1hd_pc.ini` than the one it was recorded with goes its own way from the
  first mouse move (Level 7 of the testkit: position off at frame 162 with the player's settings; at frame 6703, as on
  the recording build, with the recording's `kz1hd_pc.ini`). **Replay a route with the `kz1hd_pc.ini` it was recorded
  with**, in a test VFS of its own.
- **Loads on other threads.** The game reacts to resources that finish loading on other threads, and in a live
  recording they land on other frames than in a replay: in Level 7 a character's facial animation
  (`func_0022F6E0`) arrives in another frame, the faces' blink timer (`func_0022B220`) starts drawing from the random
  generator earlier or later, and from there the AI and the player's path differ. Of the eight testkit level routes
  only Level 4 replayed exactly. Replays agree with each other, though (eight Level 7 replays between 106 and
  142 fps: the same frames and the same position differences in all).

## Rebasing a route

`KZ_ROUTE_REBASE=<file>` next to `KZ_ROUTE_PLAY` (and `KZ_ROUTE_LOOSE=1`) writes what the replay gave the game and
what it found as a new route: the same input, the frame clock values, and the position and random generators this
build reaches. Later replays on the same build, settings and start follow it exactly. The writer is shared with the
recording (`port/kz1hd/route_rebase.h`, host test `tools/tests/test_route_rebase.sh`).

Result (2026-11-09, testkit Level 1 and 7 routes, the recording's `kz1hd_pc.ini`): each rebased route replayed twice,
headless, unpaced, loose: **0 position differences, no random-generator difference and no desync to the end**
(15305 and 14961 frames), against 10340 and 8258 frames off the recorded position before.

A rebased route belongs to one build's behaviour: a change that alters the game's logic (a lift of another EBOOT
translation, a fix in the runtime that changes timing the game reacts to) can make it diverge; the replay says so
(`position differs`, `random state differs`). Rebase again on the new base when that is intended. A change that
should not alter the game (a faster PSGL path) must keep 0 differences: the route is also a check.

## A/B runs

```bash
KZ_VFS=work/vfs-route ROUTE_SAVE=<saves>/fase1 \
  tools/perf/ab.sh <A binary> <B binary> <route> <level n> <pairs> <out dir>
```

- Runs A and B alternately (`AB_START=B` starts with B), each run under a benchmark lock (`BENCH_LOCK`, default
  `work/bench.lock`; point every script that starts the game at the same file: one game at a time on the machine), headless, 144 Hz clock (`KZ_TICK_HZ=144 KZ_GUEST_VBLANK_HZ=144`), loose.
  `A_ENV`/`B_ENV` set variables for one side only (an environment-switch A/B on one binary). `tools/perf/route_play.sh`
  is one run; it refuses the player's `work/vfs` and replaces the test VFS's savedata with `ROUTE_SAVE`.
- `tools/perf/ab_report.py <out dir>` (run at the end): per run fps, 1 % and 0.1 % low, p50 and p99 frame time, the main
  PPU thread's share and **its CPU time per frame** (from `perf.cpp`, which reads `/proc/self/task/<tid>/stat` at the
  first frame after the load and at the end), loadavg and the other processes using CPU (an instant `top` reading at
  40 s), frames and position differences (a warning when the runs did not all replay the same frames); then the
  paired difference B − A with a 95 % interval (Student's t over the pairs).
- The main thread's CPU per frame is the number to watch for main-thread work (PSGL, the engine): the main thread is
  the limit at 144 Hz (94–96 % busy in every run), and CPU per frame does not move when a run loses time waiting
  (a `KZ_JM_SKIP_US=10` run at 106 fps had the main thread 82 % busy at the same CPU per frame as the 119 fps runs).
- Session drift is large on a desktop that is in use: the same Level 7 replay went from 141.8 to about 119 fps over a
  day. Only paired runs are comparable; close the browser for small differences.
