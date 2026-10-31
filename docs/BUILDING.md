# Building

The tested route is Linux x64: a native Linux build, and a Windows x64 exe cross-built on Linux. Building on
Windows itself is not supported yet (the installer that will do it is on the [roadmap](ROADMAP.md)). As of
2026-10-30 every step below was run on the maintainer's machine (CachyOS, clang 23, an NVIDIA RTX 3070 Ti).

## The short way

After getting the code (step 1) and the tools (the table in step 2; Python's packages come by themselves), one
command does steps 2 to 4:

```sh
tools/install.sh
```

It asks where your files are (Enter keeps the path it offers), checks them as `tools/check-game-files.py` does,
then makes the venv, patches, lifts, prepares `work/vfs` and builds, with one progress bar. The paths are kept in
`work/paths.env`, which every script reads (a `KZ_*` variable you set still wins), so afterwards `tools/play.sh`
needs nothing else; it also offers an applications-menu entry with the project's icon (`tools/release/firstrun.py
--remove-desktop` takes it out). Run it again after an update or to point at other files: finished steps are skipped, and a
step that was stopped or failed is where the next run starts. The log is in `work/logs/` (your home folder written
as `~`). Without questions: `tools/release/firstrun.py --eboot PATH ...` (`--help`, `--dry-run` shows the plan).
It is `tools/release/firstrun.py`, after Project Pyrrhus's and Project Helghast's first-run builder.

## 1. Get the code

```sh
git clone --recurse-submodules https://github.com/gustavomorinaga/project-vekta
cd project-vekta
```

Or, from a release, the source archive (`project-vekta-X.Y.Z-source.tar.gz` on the
[releases page](https://github.com/gustavomorinaga/project-vekta/releases)): it is the release's tree with ps3recomp at
its pinned commit, so no submodule step. Check it first, then unpack it:

```sh
sha256sum -c project-vekta-X.Y.Z-source.tar.gz.sha256
tar xzf project-vekta-X.Y.Z-source.tar.gz
cd project-vekta-X.Y.Z
```

The steps below are the same from there; only `tools/check-tree.sh` and the lint need a git checkout.

An existing clone without the submodule: `git submodule update --init`. On Windows checkouts keep
`core.autocrlf=false`: some ps3recomp sources are CRLF and the patches carry those bytes.

## 2. Tools

| What | Arch / CachyOS packages | Notes |
|---|---|---|
| C/C++ compiler | `clang lld llvm` | clang is what the scripts call; `llvm-profdata` for `tools/pgo.sh` |
| Python 3.10+ | `python` | plus a venv for ps3recomp's tools, below |
| Vulkan | `vulkan-headers vulkan-icd-loader glslang spirv-tools` | a Vulkan driver for your GPU |
| Libraries | `sdl2-compat zlib` | SDL2 (or sdl2-compat over SDL3) |
| Movies | `ffmpeg` | loaded at run time; without it the game runs and skips the movies |
| Windows cross build | nothing more: `tools/build-win.sh` fetches its pinned toolchain on the first run | (`KZ_WIN_TOOLCHAIN=system`: `mingw-w64-gcc` and, from the AUR, `mingw-w64-sdl2 mingw-w64-zlib mingw-w64-glslang`) |

Other distributions have the same packages under their own names. CMake and Ninja come from the venv:

```sh
python3 -m venv work/venv
work/venv/bin/pip install cmake ninja -r third_party/ps3recomp/requirements.txt
```

`tools/env.sh` puts `work/venv/bin` first on the `PATH` of every script and uses its Python (`KZ_PY`).
`tools/check-deps.sh` (`--win` for the cross build) says what is still missing.

**Optional, the sandbox:** `tools/sandbox.sh [--gpu] -- COMMAND` runs a command with the system, the repository and
your game files read-only, only `work/` and a private `/tmp` writable, and no network (bubblewrap, Linux only). For
example `tools/sandbox.sh tools/lift.sh`, or `tools/sandbox.sh --gpu tools/play.sh` for a run with a window. Checked
by its host test (2026-10-30); a full lift, build and run inside it is not tried yet.

## 3. Your files

Put your files where [GETTING-FILES.md](GETTING-FILES.md) says (or point the `KZ_*` variables at them) and check
them:

```sh
tools/check-game-files.py
```

## 4. Patch, lift, build

```sh
tools/ps3recomp-patched.sh   # work/ps3recomp-patched: ps3recomp v0.18.0 + port/patches, in series order
tools/lift.sh                # work/lift (the game's code as C++) and work/fw (libsre); a few minutes
tools/setup-vfs.sh           # work/vfs: the folder the game runs from, with the movies taken out of the archive
tools/build.sh               # work/build/KZ1HD
```

- `tools/lift.sh` translates with all your cores; the build has about 220 steps, and the lifted PPU code takes
  most of the time and memory. On a machine with little memory, build with fewer jobs: `ninja -C work/build -j N`
  after `tools/build.sh` has configured it.
- `KZ_MARCH` sets the CPU target (default `x86-64-v3`, AVX2; `x86-64-v4` for AVX-512 machines).
- After changing `port/patches/`, run `tools/ps3recomp-patched.sh` and `tools/lift.sh` again (the lifters are
  patched too), then `tools/build.sh`.
- Optional, about 15 minutes: `tools/pgo.sh` plays an instrumented build into Level 1 for ten minutes and rebuilds
  `work/build` with the profile (about 10 % faster in the maintainer's runs). Run it again after every lift: a profile
  of other code is silently ignored. It plays from `work/vfs-test` (a copy of `work/vfs`, so your saves are safe).

## 5. Play

```sh
tools/play.sh
```

A window opens; `KZ_FULLSCREEN=1` starts borderless. Keyboard and mouse, controllers, the PC options page and the
files it writes (`kz1hd_pc.ini`, `kz1hd_keys.ini`, saves under `gamedata/`) are described in
[FEATURES.md](FEATURES.md). The log goes to `work/play.log`.

Headless runs for tests: `PS3RECOMP_VK_WINDOW=0 tools/run.sh SECONDS LOG`, with `PAD_SCRIPT="seconds:buttons,..."`
for scripted input (see `tools/run.sh` and `docs/dev/testing.md`).

## Tests without the game

What CI runs on every push and pull request needs no game files and no GPU: `tools/ps3recomp-patched.sh`,
`tools/check-tree.sh`, `tools/lint.sh` (shellcheck and ruff) and `tools/run-host-tests.sh` (the list is
`tools/host-tests.txt`; bash, Python 3, a C compiler and bubblewrap). Write a new test in `tools/tests/` and add it to the list.

## Windows x64 (cross-built on Linux)

```sh
tools/build-win.sh           # work/build-win/KZ1HD.exe, with the DLLs it needs beside it
```

It uses the same lift as the Linux build and the pinned toolchain of `tools/release/toolchain.json`, the one the
Windows installer will bundle: llvm-mingw (clang, lld, libc++; `port/cmake/llvm-mingw.cmake`), SDL2, zlib, glslang
and the Vulkan headers. On the first run `tools/release/fetch_toolchain.py` downloads them into
`work/toolchain-cache` and checks each against its SHA-256, and `tools/release/build_win_deps.py` builds zlib and
glslang with that compiler into `work/win-deps` (about half a minute); later runs reuse both. The exe links its C++
runtime statically, so only `SDL2.dll` and FFmpeg's DLLs go beside it (FFmpeg 9, BtbN's LGPL build, the one the
maintainer tested; the movies need them); `vulkan-1.dll` comes with the GPU driver. On the maintainer's machine the
first run took two minutes, and the exe held 144 fps under Wine and Proton (`tools/wine-test.sh`). `KZ_WIN_TOOLCHAIN=system` builds the earlier way,
with the system clang and the distribution's MinGW-w64 packages (`port/cmake/clang-mingw.cmake`). clang's native TLS
avoids the cost of the thread-locals MinGW's GCC emulates.

- **Movies on Windows:** the FFmpeg 9.x DLLs (`avcodec-63`, `avformat-63`, `avutil-61`, `swresample-7`,
  `swscale-10`) must be beside the exe; `tools/build-win.sh` copies the pinned ones there (with
  `KZ_WIN_TOOLCHAIN=system`, put an "lgpl-shared" build's there yourself). Without them, or with `KZ_NO_MOVIES=1`, the
  game logs it and goes on without movies.
- **Under Wine or Proton on Linux:** `KZ_BIN=work/build-win/KZ1HD.exe tools/play.sh` (`KZ_WINE` picks another
  runner). For Proton outside Steam, a one-line script works as `KZ_WINE`:
  `STEAM_COMPAT_DATA_PATH=<prefix> STEAM_COMPAT_CLIENT_INSTALL_PATH=$HOME/.steam/steam exec <proton dir>/proton run "$@"`.
  Killing that script does not stop the game: `WINEPREFIX=<prefix>/pfx <proton dir>/files/bin/wineserver -k`.
- **Checking it runs under Wine or Proton:** `tools/wine-test.sh` (Wine, or `--proton <proton dir>`) runs the exe
  for 60 s inside `tools/sandbox.sh`, with no window, no sound and a prefix of its own in `work/wine/`, and passes
  when the game keeps presenting at least 20 frames a second over the last 20 s (`--seconds`, `--min-fps`,
  `--window` to watch it). The first run makes the prefix.
- **The log:** the exe is a GUI program. Started from a shell, it logs to stderr; without one (double click, Steam,
  Proton) it writes `kz1hd.log` beside the exe. `KZ_LOG=<file>` picks another file, `KZ_CONSOLE=1` opens a console.
- **On a Windows PC:** copy a folder with the exe and its DLLs, `work/fw/out/libsre_image.bin`, your `dev_flash`,
  and `work/vfs` with real files instead of links (`cp -L`: Windows does not follow these links), about 4.2 GB, and
  start the exe from inside `vfs` with the path of `PS3_GAME/USRDIR/EBOOT.elf`, setting the same variables as
  `tools/play.sh` (`KZ_LIBSRE_IMAGE` and `PS3_DEV_FLASH` pointing into the folder). This is a test setup, not a
  release; the installer will replace it.

Notes from the Windows bring-up (what changed, speed under Wine and on Windows):
[docs/dev/windows-build.md](dev/windows-build.md).

## Building on Windows or macOS

Not supported yet. The scripts are POSIX shell and expect the Linux toolchain above. On Windows, WSL2 with the Linux
steps produces the Windows exe through `tools/build-win.sh` (not tried by the maintainer).
