# What goes online and where your data goes

The short version is in the [README](../README.md#what-goes-online-and-where-your-data-goes).

## What goes online

Nothing, unless you choose it. The port, the setup program and the build make no network connection of their own.
The downloads are the ones you start: the release you download, or this repository with its ps3recomp submodule,
your system's packages and the build tools' Python packages. Three more happen only when you choose them:

- **NVIDIA DLSS**: only if you accept NVIDIA's DLSS SDK licence when `tools/install.sh` or `scripts\run.cmd` asks,
  tick the Windows setup's DLSS box (unticked by default), or pass `--dlss accept`. The build then downloads the DLSS
  SDK's files from NVIDIA's repository on GitHub (`raw.githubusercontent.com/NVIDIA/DLSS`, pinned to v310.9.16 and
  checked by SHA-256) into `work/toolchain-cache/dlss-sdk/`. The question first downloads the licence's text from
  there, for you to read. Declined, nothing is downloaded and the build has no DLSS.
- **The official RPCS3, for a game straight from the disc**: only when you choose `--disc` or the setup's "From my
  Killzone Trilogy disc" (no RPCS3 of yours installed), the build downloads RPCS3 0.0.43-20640 from RPCS3's own
  GitHub releases (`github.com/RPCS3/rpcs3-binaries-linux` or `-win`, checked by SHA-256) into
  `work/toolchain-cache/rpcs3/`, to install the disc's package and decrypt its EBOOT there. Nothing is downloaded
  from Sony.
- **New-version check** (off by default): turned on with `KZ_VERSION_CHECK=1`, or with
  `tools/release/version_check.py --enable` in an install made by the setup program. The launcher then asks GitHub's
  public API for this project's latest release (`api.github.com/repos/gustavomorinaga/project-vekta/releases`) at
  most once a day, before the game starts, and names a newer one. The request carries no identifier; nothing is
  downloaded or installed. `tools/release/version_check.py --disable` or `KZ_VERSION_CHECK=0` turns it off.

A game launcher that installs the port (RecompLauncher, Quiver Launcher) makes its own connections under its own
rules; the port adds none to them.

**Project website** (`site/`): viewing the main page loads the Rajdhani font from Google Fonts, which sees your
visit. The plain page (`site/plain.html`) loads no fonts. The repository, the setup and the game do not.

## Where your data goes

Everything stays in the folder the game runs from, `work/vfs/` (made by `tools/setup-vfs.sh`):

| What | Where |
|---|---|
| PC settings | `kz1hd_pc.ini` |
| Key map | `kz1hd_keys.ini` |
| Saves | `gamedata/dev_hdd0/home/00000001/savedata/` |
| Local trophy log | `gamedata/trophies/` |
| Log of the last run | `work/play.log` |

Nothing is written outside the install's `work/` folder, and nothing is written into your game files or your RPCS3
folders. The Windows setup program is the one exception: unless you pick its "Portable" option, the settings and
saves go to `%APPDATA%\Project Vekta` instead (`scripts\play.cmd --move-data portable` or `--move-data appdata` moves them later, [INSTALL.md](INSTALL.md)).
