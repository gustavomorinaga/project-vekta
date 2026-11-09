# Known issues

Last checked 2026-11-09. Each item says what happens, where, and what to do about it today. Details for developers
are linked.

## Game

- **First-person weapon "ghosting" in the 144 Hz mode.** In busy places (the concrete yard with the Helghast
  dropships in an early level), on rare frames the whole first-person weapon shows its previous frame's pose while the
  world moves; the PS3's motion blur then doubles it. Only with the game clock at 144 Hz; the default 60 Hz is not
  affected. A first fix (the SPURS policy module's `selectWorkload` answer, after Project Helghast) removed the more
  common case where single weapon parts jumped. Workarounds: play at 60 Hz, or set `KZ_JM_SKIP_US=10` (about 15 %
  slower in Level 7). Investigation: [dev/ps3recomp-v018-migration.md](dev/ps3recomp-v018-migration.md).
- **The guest heap to watch in long sessions (2026-11-08).** Not a known fault: in a 41-minute session at 144 Hz (the
  end of Level 3 to the start of Level 5) the game's heap rose and fell with each level load, between 63 and
  188 MB, and ended at its highest, 188 MB; earlier sessions that day ended at 164 to 180 MB. It drops at several
  loads, so it is not a steady leak. If a very long session ever crashes or stutters while a level loads, look
  at it first: `KZ_STATS=1` (on in `tools/play.sh`) logs it every 2 s as `heap=` in the `[kz-stats]` lines of
  `play.log`.
- **White and black blotches on some foliage (2026-11-09).** In Level 6 (after its midpoint movie), at 3440x1440
  with the field of view at 120, the palm leaves at the screen's far left showed white and black blotches instead of
  their edges, as if their alpha test or blending went wrong. Seen once in a player's screenshot; not yet
  investigated (whether it also happens at 16:9 and the game's FOV, and which draws). Low priority. To look into it:
  an F12 capture where it shows, and the leaves' draws in its `draws.txt` (blend, alpha test, texture format).
- **Levels 2 to 11 not each played to their end.** All load and reach gameplay, and parts were played by hand; a
  problem late in a level may still be found. The lighting at the start of level 11 (Hope) is to be checked.
- **Slow memory growth** of about 0.24 MB per minute standing still in Level 1 (15 minutes, 2026-10-17). Not a
  problem in a play session.
- **No on-screen trophy notice.** Trophies unlock and are logged to `gamedata/trophies/NPWR03819.log`.

## PC

- **144 fps is not steady.** The 144 Hz mode runs at about 131 to 134 fps in Level 1 and Level 7 on the maintainer's
  machine, limited by the game's main PPU thread ([dev/performance.md](dev/performance.md)).
- **Aspect ratios narrower than 16:9** (16:10, 4:3) show 16:9 with bars.
- **The internal resolution scale applies on the next start**, not at once.
- **Surround sound** (5.1, 7.1) was checked channel by channel, not yet heard on a real surround system.

## Windows

- **Large black blocks over the frame whenever it has bars** (AMD Radeon 8060S, issue #17): the menus and the opening
  movie are partly covered in a 1280×800 window and in borderless 1920×1200 or 1920×1080 on a 1920×1200 desktop, and
  not in fullscreen 1920×1080. The present cleared the bars and copied the frame with no barrier between the two,
  which the Vulkan validation layer reports on any GPU (2026-11-07). Fixed in 0.4.0 (patch 2053), confirmed on that
  card in borderless 1920×1200 and a 1280×800 window with the 0.3.1 test build (2026-11-08). Workaround in 0.3.0:
  fullscreen at a 16:9 resolution.
- **Stutter in the opening movies and the menus, and slow menu input** (AMD Radeon 8060S, issue #18), while play
  itself holds 60 fps. With V-Sync (FIFO, the only present mode that driver offers) the menus ran 400 to 650 frames
  ahead of the screen, because `PS3_GCM_MAX_QUEUED_FLIPS`'s wait ended at once on Windows; reproduced with the Windows
  build under Wine (2026-11-07). Fixed in 0.4.0 (patch 2055; since patch 2058 the wait also naps 100 us between its
  checks instead of at least 1 ms), confirmed on that card with the 0.3.1 test build: smooth movies and menus,
  `queued` 0 to 1 (2026-11-08).
- **A fullscreen window that loses the focus goes headless** in 0.3.0 (issue #17): the game keeps running with no
  picture. Fixed in 0.4.0 (patch 2054): the picture came back after Alt+Tab on Windows 11 with an NVIDIA card
  (2026-11-07); since patch 2057 no failure to recreate the swapchain closes the window (the frames are skipped and
  the presents try again). Workaround in 0.3.0: borderless or a window.
- **Borderless and fullscreen draw at a lower resolution when Windows scales the desktop** (seen on a 1920×1200
  display, likely at Windows' 150 %, by issue #18's tester, 2026-11-09): the port does not tell Windows it handles DPI
  scaling, so Windows gives it a desktop of 1280×800 and stretches the picture; the log shows `window swapchain
  1280x800` whatever size the PC page asks for, in borderless and fullscreen. Not seen on Linux or at 100 %.
  Workarounds: set the display's scale to 100 %, or in the Properties of `KZ1HD.exe` (in `work\build-win`),
  Compatibility, "Change high DPI settings", tick "Override high DPI scaling behavior" with "Application".
- **A controller that is detected but gives no input** (a OneXPlayer X2 Mini Pro's built-in Xbox-style pad, issue
  #19). Fixed in 0.3.0 (patch 4012), not yet confirmed on that machine; in 0.2.0 set `SDL_JOYSTICK_RAWINPUT=0`
  before `play.cmd`.
- **Slower under Wine and Proton:** about 100 fps in Level 7 against about 134 for the Linux build (144 Hz mode,
  2026-10-28). On a real Windows, a 7-minute session held 140 to 144 fps most of the time with drops to 100 to 120.
- **No MangoHUD row** on the PC page of the Windows exe (MangoHUD is a Linux Vulkan layer; under Proton use it from
  the Linux side).
- **The setup program was tried on one real Windows** (the maintainer's, 2026-10-31: the first build took about four
  minutes, then the game held 60 fps; 0.3.0's installed over 0.2.0 there on 2026-11-06 and played), not yet on a
  clean Windows machine, and the full campaign was not played on Windows.
- **The port is built on the player's PC**, by the setup program or on the first start (about four to five minutes,
  and about 1.6 GB more on disk): the game is not shipped, so it cannot be prebuilt.

## Building

- Only the tested EBOOT works: the lift has its addresses built in ([GETTING-FILES.md](GETTING-FILES.md)).
- On Windows the build runs only from the release folder or the setup program (`run.cmd`), not from a Git clone;
  macOS is not supported.
