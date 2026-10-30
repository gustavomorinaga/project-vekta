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
- `KZ_UNLOCK_LEVELS=1` makes every level selectable: START, X (GAME), X (Campaign), X (select level), DOWN × (level −
  1), X, DOWN (Normal), X, X (character).

## Pictures

- `PS3RECOMP_VK_DUMP=dir/f%06u.ppm` with `PS3RECOMP_VK_DUMP_EVERY=<frames>` or `PS3RECOMP_VK_DUMP_MS=<ms>` writes frames;
  `tools/ppm_sheet.py` makes a contact sheet.
- F12 (or `PS3RECOMP_CAPTURE_TRIGGER=<file>`) captures the next frames' passes, textures and draws.
- `RSX_ENGINE_PASS_DUMP*`, `RSX_ENGINE_PS_DEBUG` and `RSX_ENGINE_STATE_LOG` dump passes and state per draw.

## Speed

Compare only in alternating A/B on the same executable; drift between sessions reaches ±3 fps
([performance.md](performance.md)). At 144 Hz use `KZ_TICK_HZ=144 KZ_GUEST_VBLANK_HZ=144`. Profiles: `KZ_PROF`,
`KZ_PROF_STACK=1`, `tools/prof_report.py`; timelines: `PS3_TRACE` and `tools/trace_frames.py`; stack samples:
`KZ_PTRACER=1` and `tools/stack_sample.sh`.
