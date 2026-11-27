# Known issues

Last checked 2026-11-27, for 0.8.0. Each item says what happens, where, and what to do about it today. Details for
developers are linked.

## Game

- **Frame-rate limits of 120, 165, 240 and Unlimited are experimental (2026-11-26).** They run the game clock at the
  limit (Unlimited: 1000 Hz, the finest the game's millisecond timer allows), as the 144 Hz mode does, so they share
  its weapon ghosting and shaking below. Above the limit the frame rate is the PC's, mostly the speed of one
  processor core: in Level 1 (2026-11-26, VADER, the maintainer's i7-11700K, the game on 4 cores, V-Sync off, 1280x720 window) 120
  held 119.9 fps (1 % low 86), 144 held 143.9 (111), 165 held 164.7 (115), 240 ran 174 (105) and Unlimited 183 (101),
  the game's main thread about 95 % busy from 165 up; the game clock kept real time within 0.1 % at every limit.
  Mouse look turns as at 60 at every limit (0.04365 degrees per count; flicks and slow sweeps injected in Level 1,
  within 1.7 % at 60, 144, 240 and Unlimited).
  A heavier place runs lower: in Level 1's opening battle on the same PC (2026-11-27, scripted input, no window)
  the 144 limit ran at 115 to 123 fps (the 0.7.0 build: 118 to 120 in the same run), 240 at 121 and Unlimited at 120,
  with the game clock exact at each.
  Reports of how they run on your machine are welcome. The default 60 is not affected.
- **First-person weapon "ghosting" in the 144 Hz mode.** In busy places (the concrete yard with the Helghast dropships
  in chapter 9, "Hidden Pasts", identified by the maintainer on 2026-11-12), on rare frames the whole first-person
  weapon shows its previous frame's pose while the world moves; the PS3's motion blur then doubles it. Only with the
  game clock at 144 Hz; the default 60 Hz is not affected. A first fix (the SPURS policy module's `selectWorkload`
  answer, after Project Helghast) removed the more common case where single weapon parts jumped. Workarounds: play at
  60 Hz, or set `KZ_JM_SKIP_US=10` (about 15 % slower in Level 7). Investigation:
  [dev/ps3recomp-v018-migration.md](dev/ps3recomp-v018-migration.md).
- **First-person weapon shaking in the 144 Hz mode (2026-11-12).** In the same yard of chapter 9, "Hidden Pasts",
  looking towards the rock face and the crashed dropship, the first-person weapon shakes strongly while the character
  model looks right. Three F12 captures (3 frames each) show the weapon moving in uneven steps (for example about 0.20
  and then 0.04 units in two frames that follow each other) with every part moving together and no frame repeating the
  previous pose, so it is not the ghosting above; with motion blur a large step looks doubled. Seen with the main
  branch's build too, and the same with `PS3_SPURS_WANTED=0` and with `KZ_FRAME_WAIT_SPIN=1`: not caused by the SPURS
  default or the frame clock wait. Cause not known yet.
- **Uneven frames at the 30 fps limit (2026-11-26).** In Level 1 with the frame-rate limit at 30, frames are shown
  17, 33 or 50 ms apart instead of an even 33 (p99 50.0 ms, 1 % low 20 fps, V-Sync off, headless; the same on the
  release branch before and after the 60 fps latency work; again on 2026-11-27, where about a quarter of the
  2-second stretches had a 50 ms frame). 60 fps is even. Workaround: play at 60.
- **One crash when a rocket exploded in Level 11 (2026-11-11).** At 144 Hz in `11.SDDockingBay` the game closed
  once when a rocket launcher shot exploded: an SPU job's host stack was 8 bytes off alignment. Not seen again in two
  full plays of the chapter. If it happens, keep `play.log` and the core and play on with the stack tripwire build
  (`[stackwatch]` lines): [dev/spu-stack-misalignment.md](dev/spu-stack-misalignment.md).
- **The guest heap to watch in long sessions (2026-11-08).** Not a known fault: in a 41-minute session at 144 Hz (the
  end of Level 3 to the start of Level 5) the game's heap rose and fell with each level load, between 63 and
  188 MB, and ended at its highest, 188 MB; earlier sessions that day ended at 164 to 180 MB. It drops at several
  loads, so it is not a steady leak. If a very long session ever crashes or stutters while a level loads, look
  at it first: `KZ_STATS=1` (on in `tools/play.sh`) logs it every 2 s as `heap=` in the `[kz-stats]` lines of
  `play.log`.
- **Multiplayer maps below 144 fps, and a wide field of view costs (2026-11-25).** Battlefields with the most bots
  (6 + 7, Team Deathmatch, 2560x1440, the CPU held at 4.0 GHz, two runs each): Canyon Crossing averages 108-110 fps,
  Delta Creek about 130, Park Terminal and Beachhead about 137, the other four 140-143. The limit is the game's main
  thread, not the GPU: Canyon Crossing sends about four times the draws of the light maps, and each costs the game's
  graphics library (PSGL) about 5 us. `field_of_view = 120` draws more of the map and costs 9-12 %: Canyon Crossing
  109 to 96 fps, Delta Creek 130 to 119 with six times as many stutters. For the smoothest play on a slower CPU, keep
  the field of view near 87. Figures and method: [dev/performance.md](dev/performance.md).
- **White and black blotches on some foliage (2026-11-09).** In Level 6 (after its midpoint movie), at 3440x1440
  with the field of view at 120, the palm leaves at the screen's far left showed white and black blotches instead of
  their edges, as if their alpha test or blending went wrong. Seen once in a player's screenshot; not yet
  investigated (whether it also happens at 16:9 and the game's FOV, and which draws). Low priority. To look into it:
  an F12 capture where it shows, and the leaves' draws in its `draws.txt` (blend, alpha test, texture format).
- **The levels are not yet each compared with reference footage** of the PS3 game. The whole campaign plays from
  start to end on Linux and on Windows (the maintainer, 2026-11-16), the start of level 11 (Hope) is lit as in RPCS3,
  and a checkpoint save works end to end (2026-11-17).
- **Slow memory growth** of about 0.24 MB per minute standing still in Level 1 (15 minutes, 2026-10-17). Not a
  problem in a play session.
- **Film grain fades out in chapter 1's fourth part** (`1.HQGrounds`, 2026-11-16). Not a fault of the port: the game
  takes its grain from 0.15 to 0 in about 30 seconds there, the same in RPCS3, so the PC page's Film Grain has nothing
  to scale. The glow likewise shows only where the game draws its source (some spots of Level 11).
- **Two players in split screen were checked with scripted input only (2026-11-26).** Game > Battlefields > Two
  Players ran on Linux with player 2 on a virtual controller (every mode and map, 37 of 37 cells), not yet with two
  people, a real second controller or on Windows. Taking player 2's controller out pauses the match; putting it back
  resumes it. While two play, AMD FSR 3.1 and NVIDIA DLSS rest and the picture is stretched from their render size,
  as with a lower Internal Resolution. Reports are welcome.
- **The European edition is not yet played through (2026-11-25).** Every level of NPEA00418 and of the European
  disc loads and reaches play on Linux, the German and Italian menus were seen, and on Windows the setup built both
  and reached the menu (a virtual machine). Not yet checked: play past each level's start, saves loaded back, and
  its Dutch, French and Spanish ([dev/editions.md](dev/editions.md)).
- **Trophies (2026-11-27).** The notice, its sound and the Trophies page were checked on Linux only, not yet on
  Windows, and no trophy has yet been earned live with RecompLauncher itself (its channel was checked with a stand-in
  launcher). Without the game's trophy set (`TROPDIR` in the game folder, [GETTING-FILES.md](GETTING-FILES.md)),
  nothing is stored and the Trophies page says the set was not found. With a debug option or scripted input on,
  trophies are not stored, by design. The trophy for a Battlefields win in split screen needs Game > Battlefields >
  Two Players. The Trophies page shows no trophy pictures (the notice does), and importing the trophies earned in
  RPCS3 is not there yet ([ROADMAP.md](ROADMAP.md)).

## PC

- **144 fps is not steady.** The 144 Hz mode runs at about 131 to 134 fps in Level 1 and Level 7 on the maintainer's
  machine, limited by the game's main PPU thread ([dev/performance.md](dev/performance.md)).
- **The sprint motion blur is stretched outside 16:9** (2026-11-12): the game draws it for a 16:9 screen (its taps
  across are 16/9 of those down, `mb_ref.c`) on the anamorphic frame, so on 21:9 it reaches further at the sides and
  on 4:3 less far; on 4:3 that is what the PS3 showed in its own 4:3 mode on a 4:3 TV. Off with `motion_blur = off`.
- **Surround sound** (5.1, 7.1) was checked channel by channel, not yet heard on a real surround system.
- **A Vulkan validation error after turning DLSS or FSR 3.1 off** (2026-11-09): the next start built the upscaler's
  pipelines ahead from the pipeline list on disk, on a device created without `independentBlend`, which the
  validation layer reports as `VUID-VkPipelineColorBlendStateCreateInfo-pAttachments-00605` (10 errors for a list of
  203 pipelines, 113 of them the upscaler's, from a Level 1 run with FSR 3.1). No picture fault was seen. Fixed after
  0.4.0 (patch 2061): the same start built the other 90 and the layer reported nothing; with FSR 3.1, all 203 and
  nothing reported.

## Windows

- **Windows, AMD RX 5000/6000: the game closed a few seconds into level 1 in 0.6.0 (#129).** AMD's Vulkan driver
  (26.10.1, RX 6600) closed the game while compiling the MLAA pass (the game's anti-aliasing as a GPU shader). A test
  build with a second form of that shader, without the 64-bit integers the driver choked on, played level 1 through
  on the same card with the MLAA on; since 0.7.0 that form is used on AMD's Windows driver. If a driver still
  closes the game in that compile, the next start leaves the MLAA out on it (the glow and motion blur stay). If it
  still happens to you, attach the support package to #129.
- **The game's `.edat` on another drive is copied** (not tried on Windows yet, 2026-11-15): without a decrypted
  `data_ps3.psarc` the run folder links the game's `data_ps3.psarc.edat`; where Windows can make neither a symbolic
  nor a hard link (the game on another drive than the port, without Developer Mode) it copies the 3.15 GB file into
  `work\vfs`, beside the 3.1 GB opened copy in `work\edat-cache`. Workaround: install the port on the same drive as
  RPCS3's `dev_hdd0`.
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
- **A controller that is detected but gives no input** (a OneXPlayer X2 Mini Pro's built-in Xbox-style pad, issue
  #19). Fixed in 0.3.0 (patch 4012), not yet confirmed on that machine; in 0.2.0 set `SDL_JOYSTICK_RAWINPUT=0`
  before starting the game.
- **Slower under Wine and Proton:** about 100 fps in Level 7 against about 134 for the Linux build (144 Hz mode,
  2026-10-28). On a real Windows, a 7-minute session held 140 to 144 fps most of the time with drops to 100 to 120.
- **No MangoHUD row** on the PC page of the Windows exe (MangoHUD is a Linux Vulkan layer; under Proton use it from
  the Linux side).
- **The shader cache on Windows is new in 0.8.0 and was run under Wine only (2026-11-27).** The game now keeps its
  compiled shaders and pipelines in the install's `work\vk-cache`, so a second run does not stutter where the first
  one compiled them; `play.log` names the folder in a `shader cache:` line. Not yet run on a real Windows.
- **Changing Internal Resolution while playing with NVIDIA DLSS was not run** (2026-11-26; no DLSS on the test
  machine): it takes the path checked with AMD FSR 3.1. Choosing DLSS in a game started without it still needs a
  restart, and its choices say so.
- **The setup program was tried on one real Windows** (the maintainer's, 2026-10-31: the first build took about four
  minutes, then the game held 60 fps; 0.3.0's installed over 0.2.0 there on 2026-11-06 and played; 0.5.0's test
  setup on 2026-11-16, and the whole campaign played there), not yet on a clean Windows machine.
- **The port is built on the player's PC**, by the setup program or on the first start (5 to 30 minutes, longer
  on a hard disk, and about 1.6 GB more on disk): the game is not shipped, so it cannot be prebuilt.

## Building

- Only the two tested EBOOTs work, the US edition's and the European edition's, both version 01.00: the lift has
  their addresses built in ([GETTING-FILES.md](GETTING-FILES.md)).
- On Windows the build runs only from the release folder or the setup program (`Project Vekta.exe`, `scripts\run.cmd`), not from a Git
  clone;
  macOS is not supported.
