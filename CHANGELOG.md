# Changelog

What each release contains, newest first, in the style of [Keep a Changelog](https://keepachangelog.com/). The port
is always built from source on the player's machine: releases carry the source and, after 0.1.0, the Windows setup
program and folder that build it. Until a release that calls itself playable
([docs/RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md)), every version is a pre-release.

## [Unreleased]

### Added
- 4:3 and 16:10 screens: the picture now fills the window instead of showing 16:9 with bars. The world's view is the
  game's own 4:3 one (its vertical FOV is 100 divided by the aspect: 75 degrees at 4:3, 62.5 at 16:10; 16:9 and
  wider unchanged), the HUD sits in the corners, menus and movies stay 16:9 with bars above and below.
- An "Aspect Ratio" row on the PC page (`aspect` in `kz1hd_pc.ini`: auto, 4:3, 16:10, 16:9, 21:9, 32:9): the
  picture at that ratio whatever the window's, with bars, and the menus and movies inside that box; applies on the
  next start.
- Crosshair Size in the PC settings: a slider for the weapon crosshair alone, from half to one and a half times the
  game's size (the middle of the slider is the game's), applied at once. It changes the crosshair's pieces, not where
  the game puts them, keeps them on whole pixels of the window so they stay sharp, and is the same share of the
  screen's height at any resolution and aspect. HUD Size no longer changes the crosshair. Original preset: the game's
  size (`crosshair_size` in `kz1hd_pc.ini`).
- Post-processing settings on the PC page, applied at once: Anti-aliasing (the game's own MLAA, or off), and
  Motion Blur, Film Grain and Glow from 0 % to 100 % in steps of 10 (`antialiasing`, `motion_blur`, `film_grain`,
  `glow` in `kz1hd_pc.ini`). 100 % is the game's own effect, unchanged; lower values scale the strength the game draws
  the effect with. The Original (PS3) preset sets them all to the game's. An older `motion_blur = on | off` reads as
  100 / 0. Colour grading stays the game's.
- Developer tools for performance work ([docs/dev/perf-bench.md](docs/dev/perf-bench.md),
  [docs/dev/performance.md](docs/dev/performance.md)): `KZ_ROUTE_REBASE` writes a route replay out as a route of the
  current build, so that later replays follow it exactly; `tools/perf/ab.sh` compares two builds in alternating runs
  on a route and reports the paired difference; ps3recomp patch 3034 (a guest store journal, built only with
  `-DKZ_STORE_JOURNAL=ON`) lets `KZ_NATIVE_VERIFY` check native functions with wide effects against their lifted
  bodies. Off unless used; the game itself is unchanged.
- Graphics debugging ([docs/dev/testing.md](docs/dev/testing.md), [docs/dev/frame-map.md](docs/dev/frame-map.md)):
  `PS3RECOMP_VK_VALIDATION=1` runs the game with the Vulkan validation layer and writes its messages to the log, each
  kind a few times and then as counts, with a total per kind at quit; under RenderDoc (`KZ_RENDERDOC=1 tools/play.sh`
  on Linux) F12 also takes a RenderDoc capture, in which every pass of the frame is named by what it draws into. Off
  unless used; the game itself is unchanged.

### Changed
- Less CPU for the same frames: the draw engine keeps the converted vertices of draws the game repeats unchanged
  (about 70 % of them) in GPU memory instead of converting them again every frame. On the Level 7 and Level 1 test
  routes the RSX thread uses 0.5-0.6 ms less per frame (about 13-15 %), the whole game about 4-7 % less; frame
  rates the same. `PS3RECOMP_ENG_VCACHE=0` turns it off (after ps3recomp pull request #718).
- `tools/pgo.sh` (optional profile-guided build): it makes its own play folder (`work/vfs-test`) from `work/vfs`
  instead of failing on a fresh clone, ends with a check that the profile matches the lifted code (a stale profile
  was silently ignored), shows a failing step's log instead of hiding it, and can train on recorded routes
  (`KZ_PGO_ROUTES`). An instrumented build now writes its profile when the game quits, so a route replay that ends
  on its own leaves one. Measured gain on ps3recomp v0.19.0: about +2 % average fps with the default play, about
  +4 % trained on combat routes ([docs/BUILDING.md](docs/BUILDING.md)).

- Less CPU while the game waits for its next frame: at the default 60 fps the main thread spun through the wait and
  stayed about 99 % busy; it now sleeps until shortly before the next tick of the game clock and takes the tick
  exactly as before, about half a core less (`KZ_FRAME_WAIT_SPIN=1` goes back to spinning). The SPURS scheduler now
  runs only the work the game asked for, without probing idle SPUs, which used 4.6 to 6 % less CPU for the whole game
  on the Level 7 and Level 1 test routes with the same frame rates (`PS3_SPURS_WANTED=0` goes back to the old way;
  after Project Helghast's patch 1074) ([docs/dev/performance.md](docs/dev/performance.md)).

### Fixed
- PC page: a row that depends on another is greyed out while that one makes it do nothing, and follows it at once:
  Resolution in borderless (the desktop's size), Aspect Ratio and HUD Position with Widescreen off, FSR Sharpness
  unless Upscaling is AMD FSR 1, Anti-aliasing while FSR 3.1 or DLSS runs (they replace the MLAA), NVIDIA Reflex
  without an NVIDIA driver that has it. A greyed row is skipped and keeps its value, with the pad and the mouse.
- PC page: the Original (PS3) preset now also sets the weapon's field of view to the game's and the temporal upscaler
  off; before, a wider weapon FOV or FSR 3.1 / DLSS stayed on after choosing it.
- PC page with the mouse: holding the button after clicking a value no longer changes the selected row once more.
- PC page: Display Mode and Resolution say "(detail on restart)": the window changes at once, but the frame is drawn
  at the new size only from the next start (until then the old size is scaled to the window).
- Borderless and fullscreen at Windows' display scaling above 100 %: the program did not tell Windows it handles DPI
  itself, so at 150 % a 1920×1200 display gave a 1280×800 picture, stretched (issue #18). It now asks for
  per-monitor DPI awareness, so the window, the swapchain and the PC page's display sizes are in real pixels
  (`SDL_WINDOWS_DPI_AWARENESS` in the environment overrides it).
- The glow of emissive effects (lights, an engine's exhaust; measured in the last level's docking bay) now shows: a
  depth buffer read as a colour texture now carries its stencil, which the game's glow uses as its mask, and the glow
  (until now only on the slow `KZ_MLAA=spu` path) runs on the GPU after the edge smoothing, equal to the PS3's chain on
  every pixel. `KZ_GLOW=<0-100>` sets its strength for tests (100, the game's, by default).
- Reservations: after many thread starts a thread could stop being told when another one broke its
  reservation (ps3recomp patch 3037, after upstream pull request #758).
- Scripted events at 60 and 144 fps: the level scripts' update ran once per frame, so above 30 fps everything they
  count in updates ran 2 to 5 times too fast. In the last level the platform ride to the main station took about
  12 s instead of about 49 s, the story events timed in seconds fell behind it and the section never ended. The
  update now runs 30 times per second of game time, as on the PS3, at every frame rate, and also on any frame with
  script messages waiting (area triggers, finished dialogue), which the game drops after the update
  (`KZ_SCRIPT_EVERY_FRAME=1` goes back to the old way). Objects a script moves step by step, such as that platform,
  slide a little every frame instead of jumping 30 times a second, which also made the voices of the characters
  riding it sound robotic (`KZ_SCRIPT_SMOOTH=0` turns the sliding off).
- Vulkan: a start without DLSS or FSR 3.1 after one with them no longer builds the upscaler's pipelines ahead, which
  the Vulkan validation layer reported as an error (`VUID-VkPipelineColorBlendStateCreateInfo-pAttachments-00605`);
  `independentBlend` is now on whenever the GPU has it (patch 2061).

## [0.4.0] - 2026-11-09 (pre-release)

### Added
- Developer switches for testing ([docs/dev/testing.md](docs/dev/testing.md)): `KZ_DEBUG_GOD=1` keeps the game's own
  God mode on, `KZ_DEBUG_GIVE` gives the player weapons and grenades once per level through the game's own
  `PLAYER:AddWeapon`, and `KZ_PROF=key,<seconds>,<file>` records a CPU profile each time F11 is pressed in the game's
  window. All are off unless set.

### Changed
- Field of view: the PC page's Field of View row is now the horizontal FOV at 16:9, 70 to 120 degrees (the game's
  is 87), with a second row, Weapon Field of View, for the first-person weapon; a wider window still shows more at
  the sides. A setting from an older version (vertical) is carried over.
- ps3recomp moved from v0.18.0 to v0.19.0: its SPURS, GCM, lv2 and file system fixes (81 pull requests). The patch
  series was re-applied: 1006 dropped (now upstream), 1017 smaller, 0006 to 0008 new; the Vulkan backend stays the
  port's. Levels 1 to 8 replay to their end; speed the same or better (levels 3, 4 and 8 faster)
  ([docs/dev/ps3recomp-v019-migration.md](docs/dev/ps3recomp-v019-migration.md)).

### Fixed
- Installing over an earlier version no longer fails at the link with undefined symbols (seen from the 0.3.1 test
  build to 0.4.0's: `ppu_thread_register_main_priority`, `ppu_thread_set_main_stack`, issue #18). The setup program
  and the release archives give every source file one fixed date, older than the objects the earlier build left, so
  Ninja took those objects as up to date and kept the earlier version's code; removing only CMake's cache did not
  change that. A build folder made from another version or patch set is now emptied and compiled again in full
  (`firstrun.py`). Workaround for an install that already failed: delete `work\build-win` (Linux folder:
  `work/build`) in the install folder and run `run.cmd` (`run.sh`) again.
- Ultrawide and wide FOV: the sky's fog band (Level 6) is no longer cut off at the screen's sides with hard grey
  edges: the sky camera's near plane now comes closer for a view wider than the game's.
- Ultrawide: the first-person weapon is no longer drawn half as large again as on a 16:9 screen (its FOV came
  from the camera's aspect, which the port widens).
- The field of view setting widens the world: before, it reached only the sky's camera, so the world stayed at the
  game's FOV and the sky and far mountains slid against it when turning.
- The pause menu's items no longer come back empty after opening one of its screens (Audio, Controls...) and
  going back: the PC item's list is made again with the game's own text format.
- VICTORY with the mouse: the statistics no longer darken under the pointer (the game keeps that list
  inactive), and Quit and Continue can be pointed at and clicked (also FAILED's Retry and Quit).
- Button prompts no longer turn back into PlayStation glyphs after a while (picking up and swapping weapons): the
  game reloads its button atlas over the same memory, and that copy now gets the player's glyphs again (patch 2060).
- Ultrawide: Rico's grenade icons stay next to his M224-A3's ammo panel instead of near the middle of the screen.
- The background of the VICTORY, FAILED and pause screens no longer turns into green static after a few levels in a
  long session: the game copies the frame into the screen's background texture when the screen opens, and with the
  MLAA on the GPU (the default) that copy was dropped, so the texture showed whatever a later level had left in that
  memory (patch 2059).
- The main menu no longer stays black after quitting from the pause menu late in a long session: every movie
  leaked one of cellAudio's 8 notify slots, so the 8th movie of a session could not start its sound and never
  played, and the menu waited for it behind its fade (patch 3033).
- Widescreen: the picture no longer shrinks to 16:9 with bars and stretches back during gameplay. A frame that took
  over 200 ms (a hitch) counted as a 2D screen; now a 2D screen must also keep showing frames without a 3D picture.
- Long sessions no longer slow down to about 70 fps from the fifth or sixth level on: the PC menu's Lua script was
  compiled again at every script call in each new Lua state once 16 states had been seen (a level makes two to five).
  It is now given to each state once (8 levels in a row: 68 to 90 fps before, 140 to 145 after).
- Black blocks over the menus and movies whenever the frame has bars (a 16:10 window or borderless display; AMD,
  issue #17): the window's present cleared the bars and copied the frame with no barrier between them, so the
  clear could land over the frame (patch 2053).
- Windows: a fullscreen window that loses the focus no longer goes headless ("the swapchain could not be
  recreated", issue #17): while minimized the window shows nothing, and the picture comes back when it is restored
  (patch 2054). Any other swapchain that cannot be recreated no longer closes the window either: the frames are
  skipped and the next presents try again, and the swapchain is also recreated after Alt+Enter, a restore or a move
  to another display at the same size (patch 2057, after Project Helghast and Project Pyrrhus on D3D12).
- Windows: `PS3_GCM_MAX_QUEUED_FLIPS` works. Its wait ended at once on Windows, so with V-Sync (FIFO, the only mode
  some AMD drivers offer) the menus and the opening movies ran 400 to 650 frames (3 to 4 s) ahead of the screen:
  slow menu input and stutter (issue #18). The Windows build under Wine now stays at 2 frames ahead (patch 2055).
  Between its checks the wait naps 100 us on a high-resolution timer, as on Linux, instead of `Sleep(1)` (at least
  1 ms), which saw each flip up to a millisecond late (patch 2058, after sp00nznet/ps3recomp#628).
- Windows: LV2 145 (`sys_time_get_current_time`) returns the wall clock, not the time since the PC started (it read
  January 1970); the Linux build was right (patch 3032). The game's own dates come through `cellRtc`, which was
  already right, so no effect in play is known. Found by Project Pyrrhus (issue #27, after its patch 6057).
- An update over an older install can no longer run the older build's translated shaders: the shader cache on disk
  (`PS3RECOMP_VK_CACHE`, default `~/.cache/ps3recomp-vk`) was keyed by the program and a version number bumped by
  hand, so a build whose shader translator or glslang changed without the bump read the older SPIR-V as its own. Its
  files and pipeline list are now keyed by the translator's id, a hash of its sources and the glslang version; the
  first start after such an update builds the cache again (patch 2056). Not seen in this port; Project Pyrrhus lost
  its sight's reticle that way (its issue #25, fixed by its patch 6047).

## [0.3.0] - 2026-11-05 (pre-release)

### Added
- Temporal upscalers on Linux and Windows (ENHANCEMENT, off by default): AMD FSR 3.1 and NVIDIA DLSS Super
  Resolution and DLAA, chosen on the PC page ("Temporal Upscaler (on restart)") or with `PS3RECOMP_TEMPORAL`, with the
  camera jittered and per-object motion vectors; the HUD is drawn at the output resolution (patches 2045 to 2052,
  [docs/dev/upscalers.md](docs/dev/upscalers.md)). FSR 3.1 is in every build, from the FidelityFX SDK's files
  vendored in `third_party/fidelityfx-fsr3/`. DLSS is downloaded from NVIDIA on your machine only if you accept
  NVIDIA's DLSS SDK licence: when `tools/install.sh` or `run.cmd` asks (`--dlss accept|decline`), or with the Windows
  setup's "Download NVIDIA DLSS" box, unticked by default; no release carries NVIDIA's files.
- The sprint motion blur runs on the GPU (patch 2043), also over an upscaled image (patch 2048).
- Button prompts: Kenney's Input Prompts (CC0) in the game's own finish, and pad pictures of our own for Xbox,
  Switch Pro and keyboard on the Controls screen, by the pad last touched (patch 4013).
- The pipelines a run used are listed and built ahead on worker threads at the next start (patch 2044).
- Developer tools: Tracy zones (patch 3029), frame-time and per-thread CPU measurement (`perf.cpp`, `tools/perf/`),
  recorded input routes replayed with the sound on the route's clock (`route.cpp`, patch 3028), opt-in guest write
  tracking (patch 3031), and the motion blur harness (`tools/mb_harness/`).
- The Windows setup program asks for your files on pages of its own (the game folder, the decrypted EBOOT, archive
  and libsre, and `dev_flash` for the music), checks their kind and size before copying anything, and builds the port
  in the wizard with a progress bar and a Stop button; when the build stops it says why and opens the log. Silent
  installs take the files as switches (`/GAME=` and the others). After Project Helghast's and Project Pyrrhus's
  setup programs (MIT). `release.yml` installs it on the Windows runner with stand-ins for the files
  (`tools/release/fake_game_files.py`) and checks that the build runs and stops at the check.
- Start menu entries "Verify and rebuild" (checks the installed files, then your game files, and rebuilds what
  changed), "Open saves folder" and "Make a support package" (a zip of the logs and settings for a bug report, without
  your user name, saves or game files). After Project Helghast's (MIT).
- The setup refuses install folders the build cannot use (`[ ] ;` in the path, OneDrive, system and profile
  folders), also for silent installs. Installing over an earlier version replaces the payload's folders whole, so
  nothing stale is left, and keeps the build, the game folder and the saves. The uninstaller asks whether to delete
  the saves and settings (default No).
- The setup program is dark, with side and header images for every screen scale and its own setup and uninstall
  icons (`tools/release/installer_art.py`).
- The Linux release folder (`tools/release/assemble_payload.py --platform linux --tar`): the port's sources, the
  patched ps3recomp and a pinned toolchain (Zig 0.17.0 as the C and C++ compiler, CMake, Ninja, SDL2, zlib, glslang,
  the Vulkan headers and FFmpeg 9's libraries), so a player needs only the usual desktop libraries (Python 3.10 or
  newer, glibc 2.31 or newer, SDL2, a Vulkan driver). `run.sh` asks for your files and builds there, `play.sh` starts
  the game. The binaries target glibc 2.31 (`port/cmake/zig-linux.cmake`, `tools/release/zig/`,
  `tools/release/build_linux_deps.py`); `KZ_LINUX_TOOLCHAIN=pinned` selects that toolchain in a source checkout too.
- Releases after 0.2.0 carry that folder as `project-vekta-X.Y.Z-linux.tar.gz`: `.github/workflows/release.yml`
  builds it and checks it in a Debian 11 container with no compiler (`tools/release/check_linux_folder.sh`).

### Changed
- The lifted PPU functions take their context as `__restrict` (patch 3030).
- A copy the setup program installed keeps its saves and settings in `%APPDATA%\Project Vekta`, so a new install or
  an uninstall leaves them alone; the setup's "Portable" option keeps them in the install folder instead. Saves an
  earlier version left in the install folder move there on the next start.

### Fixed
- Windows: the launcher stopped the game the moment it started whenever the last session's log (`work\play.log`)
  ended with its window being closed: it read that old log before the new game had emptied it. An install over an
  earlier version kept that log, so the game never opened until the old version was uninstalled; any start after a
  session closed by its window could hit it too. The launcher now empties the log before it starts the game.
- Installing over an earlier version: on a tester's Windows machine the game started and closed again at once, with
  nothing on screen, until the old version was uninstalled (its log went with the uninstall). The lift now starts
  from empty output folders, so no source an earlier version lifted is compiled in; the build now configures CMake afresh when the build folder was made from another
  version or patch set (`firstrun.py`); the launcher rebuilds before playing when the build there is not the one the
  installed files make (another version, patch set or DLSS choice, as when the setup's build was skipped or stopped);
  and a game that stops within 20 seconds of starting gets a message with the end of its log instead of silence.
- The Linux folder's build no longer stops at the compile step with "unknown CPU: 'x86'": its Zig wrappers spell
  the port's `-march=x86-64-v3` the way Zig names CPUs (`x86_64_v3`).
- The Linux folder's link no longer fails on `mallinfo2` (glibc 2.33; the folder targets 2.31): the stats line's
  heap count looks it up at run time and falls back to `mallinfo`.
- The limit of flips the game may queue ahead of the screen (`PS3_GCM_MAX_QUEUED_FLIPS`, default 2) no longer drifts:
  each flip done at once, instead of queued in the command buffer, loosened it by one for good (patch 2040). The
  `queued` value in the `[kz-stats]` lines stays at the real count.
- Windows: a controller that was detected but gave no input (seen on a OneXPlayer X2 Mini Pro's built-in Xbox-style
  pad, issue #19) works without setting `SDL_JOYSTICK_RAWINPUT=0` by hand: the port turns SDL's Raw Input joystick
  driver off (patch 4012). Setting `SDL_JOYSTICK_RAWINPUT=1` brings it back.
- GitHub shows the licence as MIT again (it read "Other", issue #27): `LICENSE` holds the MIT text alone, and the
  note that it covers only the port, not the game, moved to `NOTICE.md` and the README. Same terms as before.

## [0.2.0] - 2026-11-01 (pre-release)

### Added
- The Windows release folder (`tools/release/assemble_payload.py`): the port's sources, the patched ps3recomp and the
  pinned Windows toolchain, with no Git or MSYS needed. `run.cmd` asks for your files and builds `KZ1HD.exe` there,
  and `play.cmd` starts it (`tools/release/launch.py`).
- The Windows setup program (`installer/vekta.iss`, Inno Setup, per-user install) and the launcher `Project Vekta.exe`
  with the project's icon, which builds on its first start and then plays (`tools/release/build_launcher.py`,
  `build_setup.py`).
- Releases carry the Windows setup program and the Windows folder as a zip beside the source archive, each with a
  `.sha256`. `.github/workflows/release.yml` builds both from the tag: the folder on Linux, the setup program on
  Windows, where it is also installed, checked against the folder's `manifest.json` and uninstalled
  (`tools/release/check_install.py`); `tools/release/release_check.py` checks the five files.
- The build steps are one Python tool, `tools/kzbuild.py` (`patch`, `lift`, `vfs`, `build`), so the same code builds
  on Windows with the pinned toolchain; `tools/ps3recomp-patched.sh`, `tools/lift.sh`, `tools/setup-vfs.sh`,
  `tools/build.sh` and `tools/build-win.sh` run it and their output is unchanged. Where links cannot be made the game
  folder gets copies of the EBOOT and the archive.
- The Windows build's toolchain is pinned in the repository (`tools/release/toolchain.json`): llvm-mingw, SDL2, zlib,
  glslang, the Vulkan headers and FFmpeg 9 (its DLLs go beside the exe for the movies), fetched and checked by SHA-256 (`tools/release/fetch_toolchain.py`), zlib and
  glslang built with that compiler (`tools/release/build_win_deps.py`); `tools/build-win.sh` uses it, so the cross
  build needs no MinGW packages (`KZ_WIN_TOOLCHAIN=system` keeps the old route). The Windows setup program
  carries the same pins.
- An applications-menu entry with the project's own icon (`assets/icon.svg`): `tools/install.sh` offers it after a
  build (`firstrun.py --desktop`, `--remove-desktop`).
- `tools/install.sh`: one command asks where your files are, checks them, then patches, lifts and builds with one
  progress bar, and keeps the paths in `work/paths.env` for every script (`tools/release/firstrun.py`, after Project
  Pyrrhus's and Project Helghast's first-run builder); a stopped or failed run resumes at that step.
- `KZ_JOBS` sets the lift's and the compile's parallel jobs (the installer picks it from free memory).

### Fixed
- `tools/play.sh` names the missing step when there is no `work/vfs` or no build yet.

## [0.1.0] - 2026-10-31 (pre-release)

### Added
- The game's own MLAA anti-aliasing, reimplemented as a GPU compute pass equal to the SPU code (patches 2035 to 2037,
  `KZ_MLAA=gpu|spu|off`); the first frame no longer waits for the driver to compile it.
- NVIDIA Reflex (Off, On, On + Boost) on the PC page, through `VK_NV_low_latency2` (patch 2038).
- In-game key capture for remapping screens (patch 4011).
- `tools/wine-test.sh` runs the Windows build under Wine or Proton for a minute and says whether it held its frame
  rate; it passed at 144 fps under both on the maintainer's machine.
- `tools/mlaa_harness` and `tools/mlaa_probe`: the lifted MLAA jobs run outside the game, test patterns and checks.
- Releases: `VERSION` holds the one version string; `tools/release/bump_version.py` moves it everywhere and turns
  this section into the release's entry; a pushed tag builds a reproducible source archive (the tree with ps3recomp
  at its pinned commit) and opens a draft pre-release with notes from this file and SHA-256 sums
  ([docs/RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md)).

### Changed
- The project page is generated whole from the repository (the feature table, the screenshots, this changelog,
  the requirements and the patch series) and laid out like the Killzone 2 and Killzone 3 ports' pages;
  `docs/FEATURES.md` gains a section on the port itself (builds, frame rate, installer).

### Fixed
- `tools/release/bump_version.py` leaves an empty `## [Unreleased]` above the entry it releases, and an empty entry is
  not news on the project page.
- `tools/ps3recomp-patched.sh` also works in an unpacked source archive, where there is no git checkout.
- An SPU write into a GPU-mapped range that overlaps one already unmapped is seen as GPU-mapped again (patch 2039),
  and the SPU poison-write diagnostic works with `SPU_POISON_DMA_WATCH` (patch 1019); both found by ps3recomp's own
  tests, which now run on the patched tree in CI (patch 0004).
- An SPU that reserves a line while another SPU's plain write to it is being copied now loses the reservation at once,
  not at the next 10 ms poll (patch 1020).

## [First public tree] - 2026-10-30

### Added
- The port of Killzone HD (NPUA80856) on ps3recomp v0.18.0 with 93 patches, built on Linux, and on Linux for Windows.
- Scripts that build it from the player's own files with no fixed paths (`tools/`), a checker for those files, and
  the function seeds of the lift as a table (`re/function_seeds.txt`).
- PC features: the in-game PC page (display, resolution, internal scale, V-Sync, frame-rate limit 30/60/144, FSR 1,
  anisotropic filtering, widescreen, field of view, HUD, presets, controllers, mouse, sound output, language, skip
  intro), direct mouse look, mouse in menus, remappable keys, device-aware button prompts, local trophies.
- Documentation in English: README, getting files, building, how it works, features, known issues, roadmap, and the
  engineering notes in `docs/dev/`.
