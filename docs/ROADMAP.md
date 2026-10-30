# Roadmap

Native PC versions (Linux and Windows, Vulkan) of the PS3 Killzone games by static recompilation with
[ps3recomp](https://github.com/sp00nznet/ps3recomp), starting with Killzone HD. Technical plan:
[dev/recomp-plan.md](dev/recomp-plan.md). The port's journal: [dev/port-journal.md](dev/port-journal.md).

Updated 2026-10-30 from the maintainer's roadmap of 2026-10-29. Tick items as they are done; each phase's
percentage comes from its items and its weight.

## Principles

1. **Fidelity first.** The first release of each game is the original running natively, with no differences in
   logic, animation, positions or textures. Improvements come after and are optional.
2. **No guesswork.** Every fix comes from the game's data or the EBOOT, is checked against reference footage of the
   PS3 game, and gets a repeatable test.
3. **One base for the three games.** Runtime, renderer, PC layer and launcher are shared; each game has only its own
   map (camera, post-processing, HUD, input).
4. **Bring your own game.** Nothing of the game in the repository: EBOOT, generated code, keys, assets. The player
   provides their own copy and the recompilation happens on their machine. No Killzone branding on releases.
5. **Our own path to performance.** Where emulating the PS3 is not enough, native implementations (multi-core CPU,
   GPU), not only the original path.

## Progress

Estimate: **about 41 % of the whole project** and **about 65 % of Killzone HD** (phases 0 to 8). The weights are
each phase's estimated relative effort.

| Phase | Weight | Done |
|---|---|---|
| 0. Environment | 3 | 70 % |
| 1. PPU translated | 3 | 100 % |
| 2. Boot to the main loop | 4 | 100 % |
| 3. SPU and SPURS | 7 | 95 % |
| 4. RSX on Vulkan | 7 | 75 % |
| 5. First level | 9 | 90 % |
| 6. Full campaign | 11 | 50 % |
| 7. PC: runtime | 7 | 45 % |
| 8. PC: the game's map | 12 | 30 % |
| 9. Killzone 2 and Killzone 3 | 22 | 2 % |
| 10. Multiplayer | 6 | 0 % |
| 11. Launcher | 4 | 0 % |
| 12. Workshop (mods and add-ons) | 5 | 0 % |

## Next steps

Performance work has been paused since 2026-10-21 at about 131 to 134 frames per second in the 144 Hz mode; its
state, what was ruled out and what is left: [dev/performance.md](dev/performance.md). Recommended order:

1. **Finish Killzone HD's campaign (phase 6).** Play levels 2 to 11 to their end and fix what shows up; check a save
   made at a checkpoint end to end.
2. ~~**Move to ps3recomp v0.18.0**~~ (done 2026-10-28): the patches re-applied, the 11 levels running, speed at the
   v0.14 level (Level 7 about 131 to 135 fps). The first-person weapon problem in the 144 Hz mode stays open
   ([KNOWN_ISSUES.md](KNOWN_ISSUES.md), [dev/ps3recomp-v018-migration.md](dev/ps3recomp-v018-migration.md)).
3. **Windows build (phases 0, 4 and 6).** Done so far (2026-10-28, [dev/windows-build.md](dev/windows-build.md)): the
   exe is built on Linux (clang + MinGW-w64), runs under Wine and Proton with movies, menus and Level 1, goes on
   without movies when FFmpeg is missing, and writes `kz1hd.log` when there is no stderr. It also ran on a real
   Windows (2026-10-29). Left: a clean Windows machine, the campaign on Windows, speed under Wine and Proton (about
   100 fps against about 134 on Linux, Level 7) and **distribution: an installer that builds the port on the
   player's machine**, the main thing left before testers without a build setup can play. Main reference:
   [Project Helghast](https://github.com/undecuple/project-helghast), which does this for Killzone 3 on v0.18.0.
4. **PC options of the runtime (phase 7):** the remaining items below.
5. **Talk with the Killzone 2 and Killzone 3 ports (phase 9) and design the launcher (phase 11).**

## Fidelity: Killzone HD

### Phase 0. Environment
- [x] ps3recomp pinned and built with the Vulkan backend on Linux
- [x] `ps3recomp_host`: the 13 test scenes pass on an RTX 3070 Ti
- [x] Windows build: clang with the MinGW-w64 sysroot, cross-built on Linux (`tools/build-win.sh`)
- [x] Vulkan on Windows in ps3recomp (loader through `LoadLibrary`/SDL, option enabled in CMake); checked under Wine and Proton
- [ ] Vulkan validation layers and RenderDoc
- [x] Moved to ps3recomp v0.18.0, re-applying our patches one by one (2026-10-28)
- [ ] Record and replay routes per level: inputs recorded per game-clock tick from a checkpoint and replayed through
      the virtual pad, for repeatable benchmarks and regression tests

### Phase 1. PPU translated
- [x] `ppu_loader`/`ppu_lifter` on the EBOOT; the port compiles and links
- [x] `tools/lift.sh` redoes the whole translation from the player's files

### Phase 2. Boot to the main loop
- [x] Run folder with the decrypted archive; the game reads `KillzonePS3.ini` and its lumps
- [x] Main loop running

### Phase 3. SPU and SPURS
- [x] SPU images, job modules and libsre translated
- [x] One frame's jobs complete; Level 1 runs
- [x] The `jm_post` WWS job manager documented ([dev/wws-jm-post.md](dev/wws-jm-post.md)) and run on the PC
- [x] Rare hang entering Level 1: no longer reproduces with `jm_post` on the PC (0 in 22 loads of the 11 levels, 2026-10-17)
- [ ] Slow memory growth: about 0.24 MB/min in Level 1 standing still (15 min, 2026-10-17). Low priority

### Phase 4. RSX on Vulkan
- [x] Menus, movies and Level 1's 3D right on Linux
- [x] Pipeline cache, continuous render passes, depth as a texture built on the GPU
- [x] Same result in the Windows build (Wine, Proton and a real Windows, 2026-10-29)

### Phase 5. First level
- [x] Level 1 playable from start to end, checked by the maintainer
- [x] Keyboard in menus and in play
- [x] Logic at 60 Hz checked (hints, movement, rate of fire)
- [x] Loading from about 8 min down to about 8 s
- [ ] `jm_post` effects the game uses elsewhere (probably depth of field in cutscenes), reimplemented on the GPU

### Phase 6. Full campaign
- [x] Movies (`cellSail` through FFmpeg) and sound
- [x] `KZ_UNLOCK_LEVELS=1`: all 11 levels selectable in the menu
- [x] All 11 levels load and reach gameplay
- [x] Crash on a Lua script error fixed (the game's own setjmp/longjmp lifted)
- [x] Continuous music and sound: the firmware's MP3 decoder (flashMP3.pic) translated
- [x] Hang with sound on (a zlib queue with two producers; PPU reservations per cache line)
- [x] Hang in Strange Company (level 4) with sound on: libsre runs on the calling thread's stack
- [x] Cutscene sound (`cellAudioAdd2chData`)
- [x] Lean game logs (about 110 KB per 8-minute session)
- [x] Part of the campaign played by hand without problems (2026-10-20)
- [x] Separate test run folder (tests do not delete the player's saves)
- [ ] Levels 2 to 11 played from start to end and checked against reference footage
- [ ] To check: the lighting at the start of level 11 (Hope)
- [x] Saves: the real title (NPUA80856) from PARAM.SFO; the profile loads and Continue returns to the checkpoint
- [ ] A save made at a checkpoint checked end to end
- [x] Trophies as a local log: names, grades and platinum from the installed game's `TROPHY.TRP`, one line per trophy
      in `gamedata/trophies/NPWR03819.log` (after Project Pyrrhus's patch 5004). An on-screen notice is missing
- [ ] Full campaign on Windows

## PC: Killzone HD

The options follow section 6 of the [plan](dev/recomp-plan.md). Cuts already decided: no screen-space reflections,
subsurface scattering, ambient occlusion, mirrors, contact shadows or ray reconstruction (they may come back as
improvements after parity); DLSS Frame Generation and XeSS postponed.

### Phase 7. Runtime (shared by the three games)
- [x] SDL window with keyboard
- [x] Vblank and flip pacing for a 144 Hz monitor
- [x] Display mode: fullscreen, borderless window, window (`kz1hd_pc.ini`, Alt+Enter)
- [x] Output resolution and the monitor's refresh rate (resizable window, aspect ratio kept)
- [x] Internal resolution scale (50 % to 200 % of the window, on the PC page; applies on the next start)
- [x] V-Sync on, off and adaptive
- [x] VRR (G-Sync/FreeSync) checked on a compatible monitor (with a 60 limit the monitor shows about 60 Hz; in that
      mode presents follow 59.94 instead of 144)
- [x] Frame-rate limit (30, 60, 144)
- [x] NVIDIA Reflex: Off / On / On + Boost on the PC page (`reflex` in the ini), applied at once; `VK_NV_low_latency2`
      with present ids, the game's thread sleeping after each flip (patch 2038). Without the extension the row reads
      "not available" (AMD/Intel get it through low_latency_layer, untested). Measured on an RTX 3070 Ti, Level 7: no fps
      cost (129.9 against 131.4 at 144) and no latency change (about 13.6 ms from simulation start to present, at 144
      and at 60 with FIFO), since the GPU works about 1 ms a frame and the delay is the guest's vblank pacing and the RSX
      drain. `PS3RECOMP_VK_REFLEX_LOG=1` prints the driver's frame reports; under Wine they come out wrong. Checked on a
      real Windows (NVIDIA): the game runs normally with it on
- [x] Anisotropic filtering: the game's own per texture, off, or 2x to 16x forced on mipmapped textures
- [x] FSR 1 and RCAS sharpening on the PC page, applied at once
- [x] Xbox, DualSense/DualShock, 8BitDo and generic controllers (SDL, together with keyboard and mouse), button
      prompts that follow the device, rumble
- [x] The tutorial hints' icons and the controller picture in Controls follow the device (keyboard and mouse, Xbox,
      Nintendo)
- [x] Stereo, 5.1 and 7.1 sound: the game is told and its own mixer fills the centre and surround channels (checked
      per channel on Linux and with 6- and 8-channel WASAPI under Wine; not yet heard on a surround system)
- [x] Voice and subtitle language: System, English (US/UK), French, Spanish (the game uses one language for voice,
      subtitles and menus; System follows the PC's language, with English for languages the game lacks)
- [x] Skip the opening movie (the logos at start stay)
- [ ] Colour precision (16-bit render targets, experimental)
- [x] In-game PC page (Options and Pause), applied at once, kept in `kz1hd_pc.ini`
- [x] Presets: Original (PS3), Performance, Balanced, Quality; Custom when nothing matches
- [x] Quitting: Quit Game in the Options sub-menu; closing the window, SIGTERM and Ctrl+C also quit (waiting for a
      save in progress)
- [x] Statistics overlay with MangoHUD (Linux)

### Phase 8. The game's map
- [x] Game logic at 60 Hz, a steady 60 fps by default
- [x] Optional 144 Hz mode (about 131 to 134 fps in Level 1 since 2026-10-21)
- [ ] A steady 144 fps (about 10 % short on the main thread; paused since 2026-10-21)
- [ ] Frame map (RenderDoc): HDR scene, depth, tone mapping, post passes, HUD, camera
- [x] Direct mouse look, with its own horizontal and vertical sensitivity and invert in the game's menu (Pause >
      Controls > Advanced Settings)
- [x] Menus with the mouse: hover, click = X, right button = O, wheel, dragging sliders
- [x] PC keyboard and mouse layout (WASD, E, Q, R, G, Ctrl, Shift, X, Tab, Esc = pause) and remapping by file
      (`kz1hd_keys.ini`)
- [x] Mouse in dialogs and options
- [ ] An in-game key remapping page
- [x] Field of view: a slider on the PC page (40 to 90 degrees vertical; the game's is 56.25); zoom keeps its
      magnification and the first-person weapon keeps the game's framing
- [ ] Post-processing toggles (motion blur, grain, depth of field, lens flare, where the game has them)
- [x] HUD size and position on the PC page
- [x] Widescreen (21:9, 32:9 and wider): Hor+ picture at the window's size, menus, movies and text in the middle 16:9,
      the HUD at the edges; on and off on the PC page
- [ ] Aspect ratios narrower than 16:9 (16:10, 4:3): today 16:9 with bars
- [ ] HDR (brightness, paper white, tone mapping)
- [ ] Shadows: resolution and distance; level of detail
- [ ] Motion vectors and camera jitter
- [ ] FSR 3.1, DLSS Super Resolution and DLAA
- [ ] Dynamic resolution scale
- [ ] FSR Frame Generation
- [x] Debug menu: `KZ_DEBUG_MENU=1` turns on the game's own Debug Options in the pause menu (God Mode, Next Level, ...)
- [ ] Postponed: DLSS Frame Generation (needs an RTX 40 or newer), Intel XeSS (SDK for Windows only)

## The trilogy

### Phase 9. Killzone 2 and Killzone 3
- [ ] Bring the trilogy together with the community ports: [Project Pyrrhus](https://github.com/undecuple/project-pyrrhus)
      (Killzone 2, Direct3D 12, accepts the Trilogy disc), [Project Helghast](https://github.com/undecuple/project-helghast)
      (Killzone 3) and [tokisaki-port](https://github.com/YYOzcan/tokisaki-port) (Killzone 2, Vulkan)
- [ ] A common ps3recomp base with the three games' fixes; generic fixes go back upstream

### Phase 10. Multiplayer
Killzone HD has no online modes (its EBOOT imports only `sceNp`/`sceNpTrophy`). Killzone 2 and 3 have offline bots
and online modes whose servers were shut down.
- [ ] Local: check what each game offers (Killzone HD's menu shows "One / Two Player") and make it work
- [ ] LAN; community servers replacing `sceNp*` matchmaking; a dedicated server the community can host

### Phase 11. Launcher
- [ ] Design the architecture (one executable per game or one process, language, interface)
- [ ] "Bring your own game" installer: find the player's files, check them by hash, translate the EBOOT and compile
      on their machine
- [ ] Library with the games, install state and port updates; global and per-game options; controller profiles
- [ ] Save and local trophy manager
- [ ] Windows and Linux builds; its own name, no Killzone branding

### Phase 12. Workshop (mods and add-ons)
- [ ] A mod API on the port's extension points (overrides of the game's functions)
- [ ] Replacement files without touching the original archive; external texture packs
- [ ] Moddable level scripts (Lua 4.0) and game data, with the tools in `tools/`
- [ ] A mod manager in the launcher; a community repository for mods
- [ ] Optional visual improvements after parity (SSR, ambient occlusion, contact shadows)

## Further out
- Direct3D 12 as an alternative to Vulkan on Windows
- DLSS Frame Generation when an RTX 40 or newer is available to test
- Intel XeSS if its SDK comes to Linux
