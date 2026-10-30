# How it works

Project Vekta runs Killzone HD's own code on a PC. Nothing of the game's logic is rewritten by hand: the PS3
program is translated to C++ ahead of time, compiled for x64, and given a PC stand-in for everything around it
(the PS3's system software, its SPU scheduler and its GPU).

| Layer | On the PS3 | In the port |
|---|---|---|
| Game code (PPU, PowerPC) | about 23,400 functions in the EBOOT | lifted to C++ by ps3recomp's `ppu_lifter.py`, compiled with clang; a handful of hot functions reimplemented in `port/kz1hd/native_ppu/` |
| SPU programs | 3 SPU ELFs, the WWS job manager's policy module and 11 job modules, plus the firmware's MP3 decoder | lifted to C by ps3recomp's SPU tools; SPURS workloads run on host threads; some job loops reimplemented in `port/kz1hd/native/` |
| System libraries (lv2, `cell*`) | the PS3 system software | reimplemented on PC APIs by ps3recomp (high-level emulation), with this project's patches |
| SPURS LFQueue | firmware `libsre` | the firmware's own code, translated like the game's (`tools/lift.sh`) and called for two functions |
| Graphics (RSX) | the game's GCM command stream | replayed on Vulkan by ps3recomp's draw engine and this project's Vulkan backend (window, present, scaling, FSR 1) |
| Movies | `cellSail` playing AVI files | `port/kz1hd/sail.cpp`, decoding with FFmpeg loaded at run time |
| Input | DualShock 3 through `cellPad` | SDL controllers, keyboard and mouse through `cellPad`, with mouse look written straight into the game's camera |
| Data | `data_ps3.psarc` read by the game's file system code | the same: the game reads its own archive from the run folder |

## The build, step by step

1. `tools/ps3recomp-patched.sh` copies ps3recomp v0.18.0 (the submodule) and applies `port/patches/` in the order of
   `port/patches/series`. The submodule itself is never edited.
2. `tools/lift.sh` reads your decrypted EBOOT: ps3recomp's loader lists its segments, imports and the functions its
   tables name; `find_functions.py` completes the list with the seeds in `re/function_seeds.txt`; the PPU lifter
   writes the C++ (`work/lift/gen`). The SPU images and job modules are extracted and lifted (`work/lift/spu`), and so
   are the firmware's MP3 decoder and `libsre` (`work/fw`).
3. `tools/build.sh` compiles the lifted code, ps3recomp's runtime and the port (`port/kz1hd`) into one program.
4. `tools/setup-vfs.sh` makes the folder the program runs in: the game's files where the PS3 would have them.

Functions the port reimplements are named in its sources (`func_XXXXXXXX` in `native_ppu/`, `*_spu_func_XXXX` in
`native/`); `tools/lift.sh` passes the names to the lifters, which emit the original as `<name>_lifted` so the native
version can fall back to it.

## Frame rate above 30

The PS3 game runs its logic at 30 Hz, paced by the console's vertical blank. The port drives the game's clock
itself and its vblank: the frame-rate limit on the PC page (30, 60 or 144) sets both, 60 being the default play
setting, and `KZ_TICK_HZ`, `KZ_VBLANK_HZ` and `KZ_GUEST_VBLANK_HZ` override them (`port/kz1hd/main.cpp`). The game runs
at most one frame ahead of the screen. Logic tied to the frame rate was checked at 60 Hz (hints, movement, rate of fire); the
144 Hz mode is optional and has one known problem ([KNOWN_ISSUES.md](KNOWN_ISSUES.md)).

## Where to read more

- [docs/dev/recomp-plan.md](dev/recomp-plan.md): the original plan and the reasoning behind it.
- [docs/dev/port-journal.md](dev/port-journal.md): the dated journal of the port, from the first lift to the
  current state.
- [docs/dev/](dev/): performance work, the ps3recomp v0.18.0 migration, the Windows build, the WWS job manager and
  its post-processing jobs, data formats, and notes on other ports.
