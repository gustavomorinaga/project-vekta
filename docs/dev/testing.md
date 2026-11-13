# Testing the port

How the port is driven and checked without hands on the keyboard. Everything here needs your own game files.

## Headless runs

`PS3RECOMP_VK_WINDOW=0 tools/run.sh SECONDS LOG` runs the game with no window for SECONDS (default 60) from the run
folder (`KZ_VFS`), with the per-wait diagnostic lines filtered out of LOG. SIGTERM is ignored by the game, so the
script ends it with SIGKILL. `KZ_STATS=1` prints a `[kz-stats]` line every 2 s: flips, queued flips, the gap between
flips, heap use and game ticks (60 flips and 120 ticks per 2 s = 60 fps on a 60 Hz game clock).

Keep test runs away from your own saves: copy `work/vfs` to `work/vfs-test` and run with `KZ_VFS=work/vfs-test`.
Delete the test save under `gamedata/dev_hdd0/home/00000001/savedata/NPUA80856` before a scripted run from the start:
an existing profile changes the menu flow.

## Scripted input

- `PAD_SCRIPT="seconds:buttons,..."` presses pad buttons at given times (`0x0008` START, `0x4000` X). From a fresh
  profile, `15:0x0008,25:0x4000,35:0x0008,45:0x4000,60:0x4000,66:0x4000,...,120:0x4000` reaches Level 1. Do not press
  START late (during the level's opening movie): the pause request queues and opens the pause menu when the level
  starts.
- `PAD_STICK="lx,ly,rx,ry,from,to"` holds the sticks in a time window; `PAD_MOUSE_POS`/`PAD_MOUSE_POS_FILE` simulate the
  mouse pointer in menus.
- `KZ_DEBUG_MENU=1` adds the pause menu's Debug Options; its Next Level ends the level on the VICTORY screen.
  `tools/menu-bg-check.sh [CYCLES]` uses it to check that the VICTORY background stays flat over several levels
  (patch 2059; needs the game, a GPU and a display).
- `KZ_DEBUG_GIVE="ChainGun:731:0,HandGrenade:3"` gives the player weapons once per level through the game's own
  `PLAYER:AddWeapon(name, ammo, secondary)` (the last one is held) and grenades after them with
  `PLAYER:PickupAmmo(name, count, 0)` (`port/kz1hd/debug_give.h`). `ChainGun` is Rico's M224-A3.
- `KZ_DEBUG_GOD=1` keeps the player from taking damage with the game's own God mode (Debug Options, Overrides), set
  every frame (`port/kz1hd/debug_god.h`), for camera and view checks in a fight.
- `KZ_HUD_SITES=1` logs each HUD drawing site once per third of the screen (`[kz-hud-site]`, `[kz-hud-site2]`),
  HUD text (`[kz-hud-text]`) and who builds the menus' 512x448 layout (`[kz-menu-ortho]`).
- `KZ_HUD_TRACE=0xADDR[,0xADDR...]` logs each rectangle those HUD drawing sites (the return addresses `[kz-hud-site]`
  prints) draw with a new size, texture coordinates or place, once (`[kz-hud-trace]`, up to 1000); `KZ_HUD_SKIP=...`
  makes those sites draw nothing, to see on screen what a site is. The weapon crosshair is `0x004CBBB4`
  (`port/kz1hd/native_ppu/crosshair.h`).
- `KZ_UNLOCK_LEVELS=1` makes every level selectable: START, X (GAME), X (Campaign), X (select level), DOWN × (level −
  1), X, DOWN (Normal), X, X (character).
- `KZ_SCRIPT_LOG=1` logs, every 300 calls, how often the level scripts' `OnUpdate` ran per second of game time
  (`[kz-script]`; 30 on the PS3 and at every clock rate); `KZ_SCRIPT_EVERY_FRAME=1` calls it once per frame again, as
  before the fix, and `KZ_SCRIPT_SMOOTH=0` lets objects the scripts move jump once per 1/30 s instead of sliding
  (`port/kz1hd/native_ppu/script_update.cpp`).

## Pictures

- `PS3RECOMP_VK_DUMP=dir/f%06u.ppm` with `PS3RECOMP_VK_DUMP_EVERY=<frames>` or `PS3RECOMP_VK_DUMP_MS=<ms>` writes frames;
  `tools/ppm_sheet.py` makes a contact sheet.
- F12 (or `PS3RECOMP_CAPTURE_TRIGGER=<file>`) captures the next frames' passes, textures and draws.
- `RSX_ENGINE_PASS_DUMP*`, `RSX_ENGINE_PS_DEBUG` and `RSX_ENGINE_STATE_LOG` dump passes and state per draw.
- `KZ_POKE=<file>` is re-read every second: a line `capture` takes an F12 capture from a script; `wv <value> <n>` logs
  the next stores of a value with guest stacks (also `PPU_WVAL=<0xvalue>`, with `PPU_WVAL_LO`/`_HI`/`_GATE`; the
  watches make the game about three times slower). The other commands are listed where it is read, in
  `port/kz1hd/main.cpp`.
- `KZ_CAM_LOG=1` logs each camera projection site once (`[kz-cam]`: near, far, FOV, aspect); the sites and what each
  draws are in `port/kz1hd/fov.h`.

## Speed

Compare only in alternating A/B on the same executable; drift between sessions reaches ±3 fps
([performance.md](performance.md)), and much more on a desktop in use. `tools/perf/ab.sh` runs two builds alternately
on a recorded route and reports the paired difference; routes recorded by playing must be replayed with their own
`kz1hd_pc.ini` and rebased first (`KZ_ROUTE_REBASE`): [perf-bench.md](perf-bench.md).

- `KZ_ROUTE_REBASE=<file>` next to `KZ_ROUTE_PLAY` (and `KZ_ROUTE_LOOSE=1`): the replay also writes what it gave the
  game and found as a new route of this build, which later replays follow exactly.
- `tools/perf/route_play.sh <binary> <route> <level n> <out>`: one headless replay from a saved profile's level list
  (`ROUTE_SAVE`, a test `KZ_VFS`; it refuses `work/vfs`); `tools/perf/ab.sh <A> <B> <route> <n> <pairs> <out dir>`: A and
  B alternately, each run under `BENCH_LOCK` (`A_ENV`/`B_ENV` for a switch on one binary), then
  `tools/perf/ab_report.py`: per run fps, lows, p50/p99, the main thread's CPU per frame and the machine's load, and
  B − A with a 95 % interval.
- Native PPU functions: `KZ_NATIVE_VERIFY=1` checks them against their lifted bodies (with a `-DKZ_STORE_JOURNAL=ON`
  build for those with wide effects), `KZ_NATIVE_PSGL=0` turns the native PSGL ones off
  ([performance.md](performance.md)).

At 144 Hz use `KZ_TICK_HZ=144 KZ_GUEST_VBLANK_HZ=144`. Profiles: `KZ_PROF`,
`KZ_PROF_STACK=1`, `tools/prof_report.py` (`KZ_PROF=key,<seconds>,<file>` records when F11 is pressed in the game's
window or the file `KZ_PROF_TRIGGER`, default `<file>.start`, appears; each press makes another file, `<file>.2`...); timelines: `PS3_TRACE` and `tools/trace_frames.py`; stack samples:
`KZ_PTRACER=1` and `tools/stack_sample.sh`.

The draw engine keeps the converted vertices of repeated draws in GPU memory (patches 2062 and 2063, on by default):
`PS3RECOMP_ENG_VCACHE=0` converts every draw again, as before; `PS3RECOMP_ENG_VCACHE_STATS=1` prints the hit rate
every 300 frames; `PS3RECOMP_ENG_VCACHE_VERIFY=1` converts every hit anyway and compares it with the stored copy byte
for byte (`vertex cache verify: N hits checked, M differed`; a copy that differs is not drawn).
