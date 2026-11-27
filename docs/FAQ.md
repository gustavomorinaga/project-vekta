# Frequently asked questions

Short answers, each with a link to the page that has the detail.

## Is this an emulator?

No. The game's PPU and SPU code is translated to C++ ahead of time with
[ps3recomp](https://github.com/sp00nznet/ps3recomp) and compiled into a native Linux or Windows x64 program. No CPU
emulator runs while you play. Around the game's own code, the PS3's system libraries are reimplemented on PC APIs,
and its graphics commands are replayed on Vulkan. More: [HOW-IT-WORKS.md](HOW-IT-WORKS.md).

## Is it a remake or a rewrite?

No. The game's logic is the game's own code. The PC options (resolution, aspect ratio, upscaling, mouse look and the
rest) are added around it and are all optional; anything that would change gameplay is off by default
([FEATURES.md](FEATURES.md)).

## Do I need the game?

Yes. This repository and its downloads hold no game code, data, assets, keys or firmware, and never will. The port is
built on your PC from your own copy of Killzone HD. What each file must be: [GETTING-FILES.md](GETTING-FILES.md).

## Which copies work?

Killzone HD, the US edition (NPUA80856), installed from your own **Killzone Trilogy** disc or bought on the
PlayStation Store, and, since 0.8.0, the European edition: NPEA00418 from the European PlayStation Store, through
your RPCS3, or a copy of the European disc (BCES-01743). Each must be version 01.00
([GETTING-FILES.md](GETTING-FILES.md)).

## Do I need PS3 firmware?

No, since 0.6.0. The music is decoded by the port itself, and the SPURS queue that used the firmware's `libsre` is
in the port's runtime ([CHANGELOG.md](../CHANGELOG.md)).

## Why does it build on my PC?

Because the translated game code is made from your files, it cannot be shipped. The setup program, the release
folders and the launchers bring their own build tools and build it once, in 5 to 30 minutes
([INSTALL.md](INSTALL.md)). Afterwards the game starts directly.

## Do I need RPCS3?

Only for one step, and only if the game is installed there: the setup asks your own RPCS3 to make a decrypted copy
of the game's EBOOT (and, for the Store copy, of its data archive). If you already have the decrypted `EBOOT.elf`,
RPCS3 is not needed at all ([INSTALL.md](INSTALL.md)).

## Does it run on Linux?

Yes, natively: the Linux folder (`./project-vekta`) or a build from source with `tools/install.sh`. The Windows
program also runs under Wine and Proton, more slowly ([STATUS.md](STATUS.md)).

## Can I install it from a game launcher?

Yes: RecompLauncher and Quiver Launcher list the port, and both install the same release folders
([INSTALL.md](INSTALL.md#installing-through-a-launcher)).

## How fast does it run?

A steady 60 frames per second by default on the maintainer's machine (the game logic runs at 60 Hz instead of the
PS3's 30); an optional 144 Hz mode reaches about 131 to 134 in Level 1 and Level 7 ([STATUS.md](STATUS.md)). The
limits of 120, 165, 240 and Unlimited are experimental ([KNOWN_ISSUES.md](KNOWN_ISSUES.md)). Your PC:
[requirements.md](requirements.md).

## Can two people play on one PC?

Yes, since 0.8.0: Game > Battlefields > Two Players, in split screen. With one controller, player 1 plays on the
keyboard and mouse and player 2 on the controller; with two, each has a controller. There is no online play: the PS3
game has none ([KNOWN_ISSUES.md](KNOWN_ISSUES.md)).

## Are there trophies?

Yes, on your PC only: the port reads the names and pictures from your game's own trophy set, shows a notice when you
earn one and lists them on a Trophies page. Nothing is sent to the PlayStation Network
([PRIVACY.md](PRIVACY.md)).

## Does anything go online?

Nothing unless you choose it: the optional NVIDIA DLSS download, the official RPCS3 for a game straight from the
disc, and the new-version check, off by default ([PRIVACY.md](PRIVACY.md)).

## Where are my saves and settings?

In the install's `work/vfs/`, or in `%APPDATA%\Project Vekta` for a non-portable Windows setup install
([PRIVACY.md](PRIVACY.md#where-your-data-goes)).

## Something went wrong. What now?

Look up what you see in [TROUBLESHOOTING.md](TROUBLESHOOTING.md). If it is not there,
[open an issue](https://github.com/gustavomorinaga/project-vekta/issues/new/choose) with the log it names.

## What about Killzone 2 and Killzone 3?

They have their own ports by undecuple: [Project Pyrrhus](https://github.com/undecuple/project-pyrrhus) (Killzone 2)
and [Project Helghast](https://github.com/undecuple/project-helghast) (Killzone 3).
