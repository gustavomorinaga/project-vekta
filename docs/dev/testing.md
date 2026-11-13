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
- `KZ_POST_LOG=1` logs, about once a second, the motion blur factor and the film grain alpha the game set and the
  value drawn with at the current strength (`[kz-post]`, `port/kz1hd/native_ppu/post.cpp`).
- `KZ_CAM_LOG=1` logs each camera projection site once (`[kz-cam]`: near, far, FOV, aspect); the sites and what each
  draws are in `port/kz1hd/fov.h`.

## Vulkan validation and RenderDoc

- `PS3RECOMP_VK_VALIDATION=1` (default off; ps3recomp patch 2064) turns on `VK_LAYER_KHRONOS_validation` when it is
  installed (Arch: `vulkan-validation-layers`; Windows: the Vulkan SDK) and a `VK_EXT_debug_utils` messenger that writes
  errors and warnings to the log as `[RSX vulkan] validation warning #n [0xID name]: ...`. Each message ID is logged
  in full 3 times, then once at 10, 100, 1000... occurrences; at quit the log gets the count of every ID, most frequent
  first (`[RSX vulkan] validation: N message(s), M distinct`). A missing layer or extension is logged and the game
  runs without it. The layer alone also comes from the loader: `VK_LOADER_LAYERS_ENABLE='*validation*'` (its messages
  then go to stdout unfiltered). When the layer offers `VK_EXT_layer_settings`, its own limit of 10 reports per ID is
  turned off, so the counts are complete. The layer halves the frame rate (Level 1's route: about 61 fps against
  about 127), so time nothing with it on.
- What it reports (2026-11-13, route replays of about 15,000 frames, NVIDIA 615.78.08, layer 1.4.363): no errors.
  Level 1: 464 warnings, all `0x9805298C Undefined-Value-ShaderOutputNotConsumed` (a guest fragment program writes a
  second colour output and the pass has one target: the write is dropped, as on the RSX); Level 7: 493, the same.
  Level 7 with FSR 3.1: the same 495 plus 14959 (one a frame) `0x013365B2
  Undefined-Value-StorageImage-FormatMismatch-ImageView` inside FSR 3.1's own luma-instability pass (`rw_luma_history`
  declared Rgba8 in AMD's shader, bound to the SDK's own RGBA16F image); no fault seen in the picture.
- `PS3RECOMP_VK_LABELS=1` (patch 2065; on with validation or under RenderDoc unless set to 0) names what the GPU does:
  a debug region for each guest pass (`pass 3: A 0x0C3A0000 640x360 RGBA8 | Z ...`: the pass's place in the frame and
  its colour targets and zeta by guest address, size and format), for the port's own passes (`jm_post MLAA`, `jm_post
  motion blur`, `temporal upscaler: DLSS` or `FSR 3.1`, `FSR 1`, `present`) and a name on every colour and zeta target
  the draw engine makes (`colour 0:0x... 1280x720 RGBA8`). Without `VK_EXT_debug_utils` nothing is named.
  [frame-map.md](frame-map.md) says what each pass is.

### Running under RenderDoc

Turn the temporal upscalers off first (PC page: Upscaler other than DLSS or FSR 3.1, or `temporal=off` in
`kz1hd_pc.ini`): NVIDIA's NGX keeps parts of DLSS private and RenderDoc does not capture them, and a capture with
FSR 3.1 is large and replays slowly. A RenderDoc layer also reports fewer device features and extensions than the
driver; the port then starts without the ones it is not given (Reflex, the upscalers), as on a GPU without them.

What changes under RenderDoc (RenderDoc 1.46 on Linux, 2026-11-13):

- RenderDoc offers no Wayland surface (`VK_KHR_wayland_surface`), so on a Wayland desktop the window must go through
  XWayland: `SDL_VIDEODRIVER=x11` (`KZ_RENDERDOC=1 tools/play.sh` sets it). Without it the game runs without a window
  and RenderDoc has no frame to capture.
- The pipeline list is not built ahead on worker threads (patch 2066): RenderDoc crashed inside its
  `vkCreateGraphicsPipelines` on those threads. Each pipeline is built when first drawn instead, so the first minute
  stutters; `PS3RECOMP_VK_PREWARM=2` forces the prewarm back.
- A Level 1 capture (1280x720 guest frame shown at 3440x1320) is about 530 MB.

- **Linux**: `KZ_RENDERDOC=1 tools/play.sh` starts the game under `renderdoccmd` (RenderDoc installed, `renderdoccmd`
  on the PATH); captures go to `work/renderdoc/kz1hd_frameN.rdc`, to open in `qrenderdoc`. From `qrenderdoc` itself
  (Launch Application): executable `work/build/KZ1HD`, arguments `<run folder>/PS3_GAME/USRDIR/EBOOT.elf`, working
  directory the run folder (`work/vfs`), and the environment `tools/play.sh` exports (`KZ_LIBSRE_IMAGE`,
  `PS3RECOMP_VK_GUEST_PROGRAMS=1`, `GCM_PARK_SKIP=1`, `PS3RECOMP_VK_WINDOW=1`).
- **Windows**: RenderDoc's Launch Application on `KZ1HD.exe` in the installed folder, working directory that folder,
  arguments as `play.cmd` passes them.
- In the game, **F12** takes a RenderDoc capture of the next frame together with the port's own pass capture
  (`capture_<date>_<time>/`, below; ps3recomp patch 2066); RenderDoc's own capture keys are turned off, so one press
  is one capture. `PS3RECOMP_CAPTURE_TRIGGER=<file>` and `KZ_POKE`'s `capture` do the same from a script. The port
  finds RenderDoc only when it is already in the process; it never loads it.
- In the capture, the Event Browser shows the regions above; the Resource Inspector shows the targets by name (the
  name gives the guest's size, 1280x720, also when the image itself is larger at another internal resolution). A guest
  pass shows as several regions of the same name when the backend restarts its Vulkan render pass inside it (around
  a clear, or a draw that reads its own target): in Level 1, about 200 for the world pass.

### The F12 capture and the vertex cache

The F12 capture writes each draw's vertices (`draws.txt`, `pass*_draw*_verts*.f32`). With the converted-vertex cache
on, a draw that hits it is not converted, and until patch 2067 the capture wrote the vertices of the last converted
draw under its name. Seen 2026-11-13 on Level 1's route, one capture 40 s into the replay: draws whose written vertices
were the previous draw's while their own vertex arrays were elsewhere, 562 of 1880 with the cache on, 8 of 2458 with
`PS3RECOMP_ENG_VCACHE=0` (real repeats), 8 of 1296 with the cache on and patch 2067. Now a capture converts every hit again and checks it against the cache
(as `PS3RECOMP_ENG_VCACHE_VERIFY=1`), so its files are the draw's own; the game itself is unchanged.

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
