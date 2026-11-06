# Known issues

Last checked 2026-11-02. Each item says what happens, where, and what to do about it today. Details for developers
are linked.

## Game

- **First-person weapon "ghosting" in the 144 Hz mode.** In busy places (the concrete yard with the Helghast
  dropships in an early level), on rare frames the whole first-person weapon shows its previous frame's pose while the
  world moves; the PS3's motion blur then doubles it. Only with the game clock at 144 Hz; the default 60 Hz is not
  affected. A first fix (the SPURS policy module's `selectWorkload` answer, after Project Helghast) removed the more
  common case where single weapon parts jumped. Workarounds: play at 60 Hz, or set `KZ_JM_SKIP_US=10` (about 15 %
  slower in Level 7). Investigation: [dev/ps3recomp-v018-migration.md](dev/ps3recomp-v018-migration.md).
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

- **Borderless fullscreen at 16:10 shows large black blocks** (1920×1200, AMD Radeon 8060S, issue #17): the menus and
  the opening movie are partly covered. Windowed mode at the same size and borderless fullscreen with the desktop at
  1920×1080 are fine. Workaround: play in a window, or set the desktop to a 16:9 resolution.
- **Stutter in the opening movies and the menus, and slow menu input** (AMD Radeon 8060S, issue #18), while play
  itself holds 60 fps. `PS3_GCM_MAX_QUEUED_FLIPS` 1 to 3 did not help. No workaround yet.
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
