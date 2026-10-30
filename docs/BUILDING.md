# Building

The tested route is Linux x64: a native Linux build, and a Windows x64 exe cross-built on Linux. Building on
Windows itself is not supported yet (the installer that will do it is on the [roadmap](ROADMAP.md)). As of
2026-10-30 every step below was run on the maintainer's machine (CachyOS, clang 23, an NVIDIA RTX 3070 Ti).

## 1. Get the code

```sh
git clone --recurse-submodules https://github.com/gustavomorinaga/project-vekta
cd project-vekta
```

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
| Windows cross build | `mingw-w64-gcc` and, from the AUR, `mingw-w64-sdl2 mingw-w64-zlib mingw-w64-glslang` | only for `tools/build-win.sh` |

Other distributions have the same packages under their own names. CMake and Ninja come from the venv:

```sh
python3 -m venv work/venv
work/venv/bin/pip install cmake ninja -r third_party/ps3recomp/requirements.txt
```

`tools/env.sh` puts `work/venv/bin` first on the `PATH` of every script and uses its Python (`KZ_PY`).

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
`tools/host-tests.txt`; bash, Python 3 and a C compiler). Write a new test in `tools/tests/` and add it to the list.

## Windows x64 (cross-built on Linux)

```sh
tools/build-win.sh           # work/build-win/KZ1HD.exe, with the DLLs it needs beside it
```

It uses the same lift as the Linux build. clang and lld build against the MinGW-w64 sysroot
(`port/cmake/clang-mingw.cmake`); clang's native TLS avoids the cost of the thread-locals MinGW's GCC emulates. The
Vulkan and FFmpeg headers are copied from your system into `work/win-headers` (headers only: `vulkan-1.dll` comes with
the GPU driver, and FFmpeg is loaded at run time).

- **Movies on Windows:** put the FFmpeg 9.x DLLs (`avcodec-63`, `avformat-63`, `avutil-61`, `swresample-7`,
  `swscale-10`, for example from an "lgpl-shared" build) beside the exe. Without them, or with `KZ_NO_MOVIES=1`, the
  game logs it and goes on without movies.
- **Under Wine or Proton on Linux:** `KZ_BIN=work/build-win/KZ1HD.exe tools/play.sh` (`KZ_WINE` picks another
  runner). For Proton outside Steam, a one-line script works as `KZ_WINE`:
  `STEAM_COMPAT_DATA_PATH=<prefix> STEAM_COMPAT_CLIENT_INSTALL_PATH=$HOME/.steam/steam exec <proton dir>/proton run "$@"`.
  Killing that script does not stop the game: `WINEPREFIX=<prefix>/pfx <proton dir>/files/bin/wineserver -k`.
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
