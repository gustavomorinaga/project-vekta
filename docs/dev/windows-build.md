# The Windows build

State on 2026-10-28: `KZ1HD.exe` builds on Linux and runs under Wine and Proton (GE-Proton 11 and proton-cachyos). It
plays the opening movie, goes through the menus and plays the maintainer's save; without the FFmpeg DLLs it goes on
without movies. On 2026-10-29 it also ran on a real Windows (the development machine's Windows partition), from a
copied folder started by a `.bat` (below). How to build and run it: [../BUILDING.md](../BUILDING.md).

Patch numbers in this note are the series of that time on v0.18.0 ([patch-numbers.md](patch-numbers.md)).

## Choices

- **Compiler:** the system's clang and lld with the mingw-w64 sysroot (`port/cmake/clang-mingw.cmake`). clang emits
  native TLS; MinGW's GCC emulates thread-locals, which Project Helghast measured as expensive on hot paths. It is the
  same result as the llvm-mingw that Project Helghast ships.
- **MinGW libraries** (Arch, from the AUR): `mingw-w64-sdl2 mingw-w64-zlib mingw-w64-glslang`.
- **Headers:** Vulkan's and FFmpeg's come from the system, copied into `work/win-headers`. Headers only:
  `vulkan-1.dll` comes with the driver, and FFmpeg is loaded at run time.
- **FFmpeg:** `port/kz1hd/ffmpeg_dyn.h` loads the libraries the first time the game opens a movie (on Linux too). On
  Windows the 9.x DLLs (`avcodec-63`, `avformat-63`, `avutil-61`, `swresample-7`, `swscale-10`; for example from
  BtbN's "lgpl-shared" build) sit beside the exe. Without them (or with `KZ_NO_MOVIES=1`) the game logs it and goes on
  without movies: the opening is skipped and the menu shows over black (`sail.cpp` hands a black frame in place of
  each movie, because the menu only draws after its background movie gives a frame, and reports the movie's end on
  the first frame request, not inside `cellSailPlayerStart`; see the comment at `blank_frame`).

## Running

`tools/play.sh` runs an `.exe` through Wine (`KZ_WINE` swaps the runner).

**Proton outside Steam:** a one-line script is enough as `KZ_WINE`:
`STEAM_COMPAT_DATA_PATH=<prefix> STEAM_COMPAT_CLIENT_INSTALL_PATH=$HOME/.steam/steam exec <proton dir>/proton run "$@"`.
Killing the script does not kill the game: after a test with `timeout`,
`WINEPREFIX=<prefix>/pfx <proton dir>/files/bin/wineserver -k`. In Steam, the exe goes in as a non-Steam game, with
the path of `EBOOT.elf` in the launch options.

**Log:** the exe is a GUI-subsystem program (no console window). Started from a shell, or by Wine from one, it logs to
stderr as on Linux. Without stderr (double click, Steam, Proton, which drops it) it writes `kz1hd.log` beside the exe,
rewritten at every start. `KZ_LOG=<file>` picks another file, `KZ_CONSOLE=1` opens a console window.

**MangoHUD:** it is a Linux Vulkan layer, so the Performance Overlay row does not appear on the exe's PC page. Under
Proton it comes from the Linux side (`mangohud %command%` in Steam).

## A folder for a real Windows

A folder with the exe and its DLLs, `libsre_image.bin`, RPCS3's `dev_flash`, the run folder (`PS3_GAME`, `game`,
`gamedata`, `cache` and the two .ini files) in `vfs/`, and a `KZ1HD.bat` that enters `vfs`, sets the variables of
`tools/play.sh` (`GCM_PARK_SKIP`, `PS3RECOMP_VK_GUEST_PROGRAMS`, `PS3_VERBOSE=0`, `PS3_GCM_MAX_QUEUED_FLIPS=1`,
`PS3RECOMP_VK_WINDOW=1`, `KZ_LIBSRE_IMAGE` and `PS3_DEV_FLASH` relative to `%~dp0`) and starts the exe with the path of
`EBOOT.elf`. Copy files, not links: in the run folder `EBOOT.elf` and `data_ps3.psarc` are symbolic links, which
Windows does not open (`cp -L`). With the archive, the folder is about 4.2 GB. It is a sketch of what the installer
will do on the player's machine.

## What changed for Windows

- **ps3recomp:** the Vulkan backend is no longer Linux-only (it loads `vulkan-1.dll` with `LoadLibrary`, and the
  window and input still go through SDL2); with it, `cellPad` uses the SDL2 backend on Windows too (keyboard, mouse and
  the `kz1hd_keys.ini` map, the same as on Linux). Also: glslang found through its CMake package, the sampler's stack
  read without `__try`/`__except` and the `ppu_resv_break` linkage (both from Project Helghast's patch 0002), the
  SPURS poll sleep and the F12 capture's `mkdir`.
- **Port:** `win_compat.h` (`setenv`, `strcasecmp`), the libsre page committed before it is read (on Windows the
  guest memory is only reserved), the heap size from `GetProcessMemoryInfo`, and `jm.c`'s private page with
  `VirtualAlloc`.

## Open items

1. ~~Proton~~ (2026-10-28: GE-Proton 11 and proton-cachyos, movie, menu and Level 1). A real Windows: ran on
   2026-10-29 on the development machine (RTX 3070 Ti), a session of about 7 minutes played by the maintainer: in the
   144 Hz mode, 140 to 144 fps most of the time, with drops to about 100 to 120; at the 60 limit, a steady 60; it
   quit cleanly on closing the window. Not the same section as the Level 7 benchmark, but it already shows native
   Windows well above Wine's about 100 and close to Linux. A clean machine is still to try.
2. ~~Without FFmpeg the game stops at the opening.~~ Fixed on 2026-10-28: without the FFmpeg DLLs the exe goes from the
   opening to Level 1 under Wine.
3. Speed: in Level 7 (a saved game, 144 Hz clock, no window) the exe ran at about 100 fps under Wine and about 101
   under GE-Proton 11, against about 134 for the Linux build (2026-10-28, two alternating runs of each, under 1 fps of
   spread; the exe without PGO, and Wine adds its own cost). The first measurement, about 114 against 128, was with a
   window. In scripted tests under Wine the boot is about 20 s slower (the whole opening movie plays before the menu),
   so timed button presses land too early; with a window, the mouse cursor over it also changes the menu selection
   (suspected, not checked). The presses need to wait for the menu (the log's `descriptor 2:` line), and even then
   the menu sometimes ignores a press under Wine. Of Project Helghast's Win64 optimisations: `getenv` out of hot paths
   (its 1127) does not pay for Killzone HD (counted with an interposed `getenv` in Level 1: about 100 calls per second
   in all, against millions per second in the case of upstream #243); the System V convention for SPU functions (its
   1099) touches setjmp/longjmp and needs a new lift, and our bottleneck is the main PPU thread, not the SPUs; SPU
   threads on P-cores (its 1136) only matter on hybrid CPUs.
4. Distribution: an installer or a portable zip that builds on the player's machine from their package (Project
   Helghast's model; [upstream-and-reference-ports.md](upstream-and-reference-ports.md)).
