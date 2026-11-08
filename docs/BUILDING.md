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

It also asks once whether to download NVIDIA DLSS: yes means you accept NVIDIA's DLSS SDK licence
(<https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt>; the question shows the text it downloaded), and the build
then fetches the SDK's files from NVIDIA's repository, checks them against the pins in `tools/release/toolchain.json`,
builds DLSS in and puts `libnvidia-ngx-dlss.so.310.9.16` beside `work/build/KZ1HD`. No means no download and no DLSS
(the PC menu shows it as "(not built)"); AMD FSR 3.1 is built either way. The answer is kept in `work/dlss-choice`;
to change it, run `tools/install.sh --dlss accept` or `--dlss decline`.

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
tools/ps3recomp-patched.sh   # work/ps3recomp-patched: ps3recomp v0.19.0 + port/patches, in series order
tools/lift.sh                # work/lift (the game's code as C++) and work/fw (libsre); a few minutes
tools/setup-vfs.sh           # work/vfs: the folder the game runs from, with the movies taken out of the archive
tools/build.sh               # work/build/KZ1HD
```

- `tools/lift.sh` translates with all your cores; the build has about 220 steps, and the lifted PPU code takes
  most of the time and memory. On a machine with little memory, build with fewer jobs: `ninja -C work/build -j N`
  after `tools/build.sh` has configured it.
- These scripts run `tools/kzbuild.py patch`, `lift`, `vfs` and `build`: the steps are Python, so the same code will
  build on Windows (`tools/kzbuild.py env` prints the paths they use).
- `KZ_MARCH` sets the CPU target (default `x86-64-v3`, AVX2; `x86-64-v4` for AVX-512 machines).
- Temporal upscalers: AMD FSR 3.1 is built from `third_party/fidelityfx-fsr3` (`-DKZ_FSR3=OFF` leaves it out), in
  the Linux build and the Windows exe. `KZ_DLSS_SDK=<the NVIDIA DLSS SDK folder>` builds DLSS in and copies its
  runtime library beside the program (`tools/build.sh` and `tools/build-win.sh` alike);
  `tools/release/fetch_toolchain.py --set dlss-sdk --accept-license [--os windows]` fetches that folder into
  `work/toolchain-cache/dlss-sdk` (the option says you accept NVIDIA's licence). See
  [dev/upscalers.md](dev/upscalers.md).
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

The exe has AMD FSR 3.1 (from `third_party/fidelityfx-fsr3`, built with the same compiler). With
`KZ_DLSS_SDK=<the NVIDIA DLSS SDK folder>` it also has DLSS, and `nvngx_dlss.dll` goes beside it: NVIDIA ships the
NGX loader (`lib/Windows_x86_64/x64/nvsdk_ngx_s.lib`) for MSVC only, and ps3recomp patch 2052 links it with MinGW.
`tools/release/fetch_toolchain.py --set dlss-sdk --os windows --accept-license` fetches the Windows files into
`work/toolchain-cache/dlss-sdk` (the option says you accept NVIDIA's licence). NVIDIA's files stay on your machine:
no release carries them ([dev/upscalers.md](dev/upscalers.md), "Distribution").

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
- **Without the game:** `tools/wine-host-tests.sh` cross-builds the host tests that also build for Windows (with the
  pinned llvm-mingw and SDL2) and runs them under Wine, in a prefix of its own in `work/wine-host/`. CI runs it on
  every push and pull request.
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

Windows: a release folder, made on Linux, builds the port on the player's PC with nothing else installed (no Git,
no MSYS, no Python), and a setup program carries it. Since 0.2.0, `.github/workflows/release.yml` builds
both from the tag and publishes them beside the source archive: the setup program, and the folder as a zip.

```sh
tools/release/fetch_toolchain.py --host windows    # the Windows entries of tools/release/toolchain.json
tools/release/build_launcher.py                    # work/release/launcher/Project Vekta.exe
tools/release/assemble_payload.py --out work/release/payload --launcher "work/release/launcher/Project Vekta.exe" \
    [--zip work/release/project-vekta-<version>-windows.zip]
tools/release/build_setup.py --payload work/release/payload   # work/release/project-vekta-<version>-setup.exe
```

`build_setup.py` compiles `installer/vekta.iss` with the pinned Inno Setup (installed on the first run, checked by
SHA-256; under Wine on Linux). The setup program installs per user into `%LOCALAPPDATA%\Programs\ProjectVekta`. It
asks where your files are, on pages of its own: the installed game folder (NPUA80856), the decrypted `EBOOT.elf`,
`data_ps3.psarc` and `libsre.prx`, and the firmware's `dev_flash`. It checks their kind and size before copying
anything and keeps them in `work\paths.env`. Then it builds the port in the wizard, with a progress bar and a Stop
button; the build's first step checks the files' SHA-256, and when it stops the wizard says why and opens its log. The
launcher (`Project Vekta.exe`) builds on its first start when that did not happen, then starts the game. A silent
install takes the files as switches: `/GAME=<dir> /EBOOT=<file> /PSARC=<file> /LIBSRE=<file> [/DEVFLASH=<dir>]`.

A page after those offers NVIDIA DLSS, with a box that is unticked by default: "Download NVIDIA DLSS (NVIDIA RTX
GPUs). I accept the NVIDIA DLSS SDK license", and the licence's link
(<https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt>). Ticked, the build downloads the SDK's files from
NVIDIA's repository, checks them against their pins, builds DLSS in and puts `nvngx_dlss.dll` beside `KZ1HD.exe`;
unticked, nothing of NVIDIA's is downloaded and the build has AMD FSR 3.1 only. The answer goes to `firstrun.py
--dlss accept|decline` and `work\dlss-choice`, and an upgrade offers it again as it was. To change it, run the setup
again (or `run.cmd --dlss accept` / `--dlss decline` in the folder); a silent install takes `/DLSS=accept`.

Saves and settings (`kz1hd_pc.ini`, `kz1hd_keys.ini`, `gamedata/`) go to `%APPDATA%\Project Vekta`: the launcher runs
the game from there. With the setup's "Portable" task (`/MERGETASKS=portable`) they stay in the install folder's
`work\vfs`, as in the zip or a Git clone. `vekta-install.ini` in the install folder records the choice. Saves an
earlier version left in `work\vfs` are moved to `%APPDATA%` on the next start, never over a file already there.

The Start menu has, beside the game: "Verify and rebuild" (`launch.py --repair`: the install's files against
`manifest.json`, then the check of your game files and a rebuild of what changed), "Open saves folder"
(`--open-data`) and "Make a support package" (`--support-package`: one zip on the Desktop with the logs and settings,
your user folder written as `~`, no saves and no game files; `tools/release/support.py`).

The setup refuses an install folder the build cannot use: one with `[`, `]` or `;` in its path (CMake), one inside
OneDrive, or a drive's root, Windows, Program Files, Documents, the Desktop, the profile or Downloads; a silent
install with such a `/DIR=` ends with exit code 7 before writing anything. Installing over an earlier version
replaces the payload's own folders whole (the sources, the tools, the patched ps3recomp and the toolchain), so a file
the new version dropped does not linger; the build, the game folder, the logs and your choices stay, and only what
changed is rebuilt. The uninstaller asks whether to delete the saves and settings too (default No; a silent
uninstall keeps them).

The setup is dark, with its own side and header images at four DPI scales and its own setup and uninstall icons,
all in `installer/art/` and made by `tools/release/installer_art.py` from the project's art in the site's palette
(`--fetch-fonts` once for Fira Sans Condensed, then run it; `--check`, which the host tests run, says when an image
no longer matches the script).

The folder keeps this repository's layout: the port's sources, the patched ps3recomp in `work/ps3recomp-patched` (so
the player needs no Git) and the toolchain in `work/toolchain-cache` (llvm-mingw, CMake, Ninja, the embeddable
Python, SDL2, zlib, glslang, the Vulkan headers and FFmpeg). On Windows, `run.cmd` asks where your files are, checks
them, asks once about NVIDIA DLSS as `tools/install.sh` does, and builds `work/build-win/KZ1HD.exe` there
(`tools/release/firstrun.py`; glslang and zlib are compiled on the first run), and `play.cmd` starts the game (`tools/release/launch.py`). Without the right to make symbolic links,
the game folder gets copies of the EBOOT and the archive. The payload's scrub refuses game files, home paths and
secrets; `manifest.json` lists every file with its SHA-256. macOS is not supported.

Linux, without a developer setup: from the release after 0.2.0 a Linux folder ships beside them, the same layout with
its own toolchain, so the player needs only Python 3.10 or newer (ps3recomp's tools), glibc 2.31 or newer (with that
Python: Ubuntu 22.04, Debian 12, Fedora 35, SteamOS 3 and later), SDL2 and a Vulkan driver, no compiler and no
packages to install. Its compiler is [Zig](https://ziglang.org) 0.17.0 (`zig cc`, pinned in
`tools/release/toolchain.json` from its PyPI wheel) aimed at `x86_64-linux-gnu.2.31` (`port/cmake/zig-linux.cmake`,
the wrappers in `tools/release/zig/`), so what it builds runs on any distribution with that glibc or newer; CMake,
Ninja, SDL2 (built to link against, the system's `libSDL2-2.0.so.0` is used at run time), zlib, glslang, the Vulkan
headers and FFmpeg 9's shared libraries (copied to `work/build/lib`, which `tools/play.sh` puts on the library path)
come with it.

```sh
tools/release/fetch_toolchain.py --host native                          # Zig, CMake, Ninja, SDL2, FFmpeg
tools/release/fetch_toolchain.py --only zlib,glslang,vulkan-headers     # the sources it also builds
tools/release/assemble_payload.py --platform linux --out work/release/linux \
    [--tar work/release/project-vekta-<version>-linux.tar.gz]
```

There, `./run.sh` asks where your files are and builds (`KZ_LINUX_TOOLCHAIN=pinned`; `tools/release/
build_linux_deps.py` compiles SDL2, zlib and glslang on the first run), and `./play.sh` starts the game. In a source
checkout `KZ_LINUX_TOOLCHAIN=pinned tools/install.sh` takes the same route. The release workflow unpacks the folder
in a Debian 11 container with no compiler, builds its libraries and ps3recomp's boot smoke test with it and runs
them (`tools/release/check_linux_folder.sh`). A launcher packaged as a Flatpak or AppImage around this folder is a
later step.
