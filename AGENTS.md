# Instructions for AI coding agents

Read this before changing anything in this repository. It applies to every automated contributor (Claude Code, Codex,
Copilot, Cursor and the like) and to the people who run them. Human contributors: the same rules are in
[CONTRIBUTING.md](.github/CONTRIBUTING.md).

Adapted from the same file in [Project Helghast](https://github.com/undecuple/project-helghast) and
[Project Pyrrhus](https://github.com/undecuple/project-pyrrhus), the Killzone 3 and Killzone 2 ports by undecuple (MIT).

## The project

Project Vekta is an unofficial PC port of Killzone HD (PS3, the HD remaster of the first Killzone, title NPUA80856,
installed from the `PS3_EXTRA` package of the Killzone Trilogy disc) made by **static recompilation**: the game's
PPU and SPU code is lifted to C++ ahead of time with [ps3recomp](https://github.com/sp00nznet/ps3recomp) and compiled
into a native Linux or Windows x64 program, and the PS3 system software it calls is reimplemented; graphics go through
Vulkan. The repository holds only the port. Each player lifts and builds the game's code from their own copy.
Overview: [README.md](README.md) and [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md). Status:
[docs/FEATURES.md](docs/FEATURES.md), [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

## Repository map

| Path | What it holds |
|---|---|
| `port/kz1hd/` | the program: `main.cpp` (boot, game clock, window), `stubs.cpp` (HLE overrides), `pcoptions.cpp` (the in-game PC menu, mouse look), `sail.cpp` (movies), `glyphs.cpp` (button prompts), `native/` (SPU jobs reimplemented in C), `native_ppu/` (PPU functions reimplemented in C++), `CMakeLists.txt` |
| `port/patches/` | numbered patches to ps3recomp and `series`, the order they apply in (see "Rules for everyone") |
| `port/cmake/` | the clang + MinGW-w64 toolchain file for the Windows cross build |
| `third_party/ps3recomp/` | the recompiler, a pinned submodule (never edited in place) |
| `tools/` | `env.sh` (paths), `ps3recomp-patched.sh`, `lift.sh`, `setup-vfs.sh`, `build.sh`, `build-win.sh`, `pgo.sh`, `run.sh`, `play.sh`; `sync-from-private.py` (the maintainer's import from the private working repositories); data and profiling tools (`psarc_ls.py`, `lua4dis.py`, `extract_spu_modules.py`, `prof_report.py`, `spu_capture.py`, ...) |
| `re/` | tables the tools read (`function_seeds.txt`); numbers only |
| `site/`, `assets/` | the project web page and its images |
| `docs/` | public documentation; `docs/dev/` holds engineering notes |
| `CHANGELOG.md` | what each release contains, newest first |

## Rules for everyone

- **No game material, ever.** Never commit, attach, paste, request or link to: files from the disc or the package,
  decrypted binaries (`EBOOT.elf`, `libsre.prx`, SPU images), lifted or decompiled game code (`work/lift/`,
  `work/fw/`), extracted assets, movies, saves, keys or firmware. Numbers, names, structure layouts and formulas
  recovered from the game are fine. `Game/` and `work/` stay out of git (`.gitignore`); do not weaken it.
- **No circumvention.** The repository ships no decryption or copy-protection circumvention tools, no keys and no
  instructions for decrypting game or firmware files. Do not add any, and do not explain how to make the decrypted
  files; the docs only say what the player must have ([docs/GETTING-FILES.md](docs/GETTING-FILES.md)).
- **ps3recomp stays pristine.** Never edit `third_party/ps3recomp/` or move the pin as part of a fix. Every change
  to it is a new patch in `port/patches/`, named in its area's number block and added at the end of
  `port/patches/series`; `tools/ps3recomp-patched.sh` applies the series in order and fails on a patch that does not
  apply or a file the series does not name. The blocks: `0xxx` general (port bring-up on a release, platform),
  `1xxx` SPU and SPURS, `2xxx` graphics (RSX, GCM, Vulkan, present), `3xxx` lifter and system libraries (PPU, lv2,
  audio, save data, tracing), `4xxx` input and file system, `5xxx` data and trophies. Take the next free number in
  the block; never renumber or reuse one.
- **No GPL code.** RPCS3 (GPL-2.0) may be read as a reference for behaviour and data layouts; never copy, translate
  or closely paraphrase its code. Write behaviour from your own understanding and say so in the change.
- **Gameplay stays original by default.** Mark features the PS3 game does not have `ENHANCEMENT:`; anything that
  changes gameplay is opt-in. Mark values not yet recovered from the binary `ASSUMPTION:` and never restate one as
  fact. Keep original function addresses in comments (`func_00291FA8`, `0x131E4DC`).
- **Online is opt-in.** Nothing may contact the network unless the player chose it; a new connection must be named
  in the README's "What goes online" list.

## Licence and attribution

- The port's code, tools and docs are MIT ([LICENSE](LICENSE)); ps3recomp is MIT; other components keep the terms
  listed in [NOTICE.md](NOTICE.md). The game itself is covered by no licence here.
- A new dependency needs a licence compatible with MIT distribution and a row in NOTICE.md in the same change.
- **Credit what you absorb.** Work taken from an upstream pull request, a fork or another port is credited in the
  patch header, in NOTICE.md and in the commit message, for example `after Project Helghast patch 1058 (MIT)`.
  Never remove or shorten existing credits.

## Build and test

Build steps: [docs/BUILDING.md](docs/BUILDING.md). **Never write into the player's game files**, and keep them out
of the repository.

- **Before every pull request** run what CI runs (no game data, no GPU): `tools/ps3recomp-patched.sh` must apply
  every patch, and `tools/check-tree.sh` must find no game files, personal paths or secrets.
- A change to the runtime or a patch is checked in the game before it is called working: say which level, how long,
  with what input (`PAD_SCRIPT`, by hand) and what the log or a screenshot showed. Headless runs use
  `PS3RECOMP_VK_WINDOW=0` (see `tools/run.sh`).
- Agents without the game can still work on the tools, the patches' host side, the docs and the site; say in the
  pull request that game runs were not done.

## Docs travel with code

- Update the docs a change affects in the same commit: build steps in `docs/BUILDING.md`, the files the player needs
  in `docs/GETTING-FILES.md`, known problems in `docs/KNOWN_ISSUES.md`, feature status in `docs/FEATURES.md`,
  credits in `NOTICE.md`, and a line in `CHANGELOG.md` for anything a player would notice.
- Never write that something works unless you ran it, and say how. Findings are dated.
- Environment switches are documented where they are read (`getenv`), with their default.

## Commits and pull requests

- One topic per commit; short, plain, descriptive messages (what changed and why), in English. Credits go in the
  message as above; no tool-generated footers, co-author trailers or advertising lines. Commits are not signed.
- The pull request says what was run and what was seen. Use the templates in `.github/`.
- Never add personal paths, user names, tokens or other secrets.

## Fixes that belong upstream

A patch that is not specific to this game (a lifter bug, a library that behaves differently from the PS3, a runtime
limit) is a candidate for [ps3recomp](https://github.com/sp00nznet/ps3recomp). Keep it free of anything
game-specific and propose it there with symptom, cause, fix and a test. The patch stays here until upstream takes it.

## Security and rights holders

Report vulnerabilities privately ([SECURITY.md](.github/SECURITY.md)): GitHub's "Report a vulnerability" on the
Security tab, never a public issue. Rights holders use the same private channel.
