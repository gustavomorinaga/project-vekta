# Third-party components and credits

Every piece of code, patch, idea or text that came from outside this repository is listed here with its author,
source, licence and what was used. Licence texts are in [licenses/](licenses/). Per-patch credit is also in each
patch's header in `port/patches/`. The port's own code is MIT ([LICENSE](LICENSE)).
That licence covers the port only: its code, patches, tools and documentation. It grants nothing in the game,
which is (c) Sony Interactive Entertainment and is not part of this repository.

## Toolkit

| Project | Author | Source | Licence | What was used |
|---|---|---|---|---|
| ps3recomp | Ned Heller (sp00nznet) and its contributors | <https://github.com/sp00nznet/ps3recomp> (submodule `third_party/ps3recomp`, pinned at release v0.19.0) | MIT ([licenses/ps3recomp-LICENSE.txt](licenses/ps3recomp-LICENSE.txt)) | the PPU and SPU lifters, the PS3 runtime and the HLE system libraries the port is built on; our changes are the patches in `port/patches/`, applied on top of that exact release |

## Other ports

| Project | Author | Source | Licence | What was used |
|---|---|---|---|---|
| Project Helghast (Killzone 3 PC port) | undecuple | <https://github.com/undecuple/project-helghast> | MIT ([licenses/Project-Helghast-LICENSE.txt](licenses/Project-Helghast-LICENSE.txt)) | the SPURS kernel's admission rule and `cellSpursRequestIdleSpu` as its patch 1074 ports them (our 1017); the policy-module `selectWorkload` answer of its patch 1058 (our 1018); the MinGW sampler stack read and `ppu_resv_break` linkage of its patch 0002 (our 0003); `tools/ps3recomp-patched.sh` and the patch step of `tools/kzbuild.py` (line-ending check, Python lookup, content-only sync); the repository layout, the patch number blocks and the shape of `AGENTS.md`, `CONTRIBUTING.md` and the docs; the shape of `tools/run-host-tests.sh`, running ps3recomp's own tests on the patched tree (`tools/tests/test_upstream_suite.sh`), the shape of `tools/wine-test.sh` (after its `tools/proton-test.sh`), the `ruff.toml` rule set, `.gitleaks.toml`, `docs/features.tsv` with `tools/features.py`, the rule of `tools/sandbox.sh`, and the CodeQL, issue-label and pages workflows; the setup's Start menu entries, its install folder checks and the support package (`tools/release/support.py`); the swap chain recreated after a mode change that keeps the size, and the skipped frame with a new try when it cannot be had (patch 2057, after their patches 2130, 2133 and 2152 and Project Pyrrhus's 6040 and 6055; written here) |
| Project Pyrrhus (Killzone 2 PC port) | undecuple | <https://github.com/undecuple/project-pyrrhus> | MIT ([licenses/Project-Pyrrhus-LICENSE.txt](licenses/Project-Pyrrhus-LICENSE.txt)) | the shape of the public documentation (README sections, getting-files and release-criteria pages) and of the project site, its generated page and stylesheet (`tools/website.py` and `site/style.css`, after its `tools/release/website.py` and `site/style.css`); the release tools and workflows (`tools/release/`, `release.yml`, `release-check.yml`, `tools/tests/test_version_agree.py`, after theirs and Project Helghast's); the pinned Windows toolchain (`tools/release/toolchain.json`, `fetch_toolchain.py` and `port/cmake/llvm-mingw.cmake`, after theirs and Project Helghast's; the llvm-mingw, CMake, Ninja and Python pins are theirs); the first-run builder (`tools/release/firstrun.py`: its steps table, markers, progress and status lines, job count and log, after theirs and Project Helghast's); the Windows release folder (`tools/release/assemble_payload.py`: its allowlist, scrub and manifest; `tools/release/launch.py`; `installer/run.cmd`; the launcher `installer/launcher/` with `tools/release/build_launcher.py`; the setup script `installer/vekta.iss` and its Inno Setup pin, its game-file pages, build page, portable choice, upgrade cleanup and uninstall question; the setup's artwork set with `tools/release/installer_art.py`'s `art.json` and `--check`; after theirs and Project Helghast's); the finding that LV2 145 read the time since boot on Windows (patch 3032, after their patch 6057 and sp00nznet/ps3recomp#633; the fix is written here); the finding that a cache of translated shaders must be tied to the translator that wrote it (patch 2056, after their patch 6047 and issue #25; the fix is written here) |
| Upstream ps3recomp pull request #758 | sp00nznet/ps3recomp contributors | <https://github.com/sp00nznet/ps3recomp/pull/758> | MIT | each reservation context registered once, so reservation breaks keep reaching every thread after many thread starts, with its test (patch 3037, ported) |
| Upstream ps3recomp pull request #628 | MaChInEgUn3 | <https://github.com/sp00nznet/ps3recomp/pull/628> | MIT | the per-thread high-resolution waitable timer for sub-millisecond waits on Windows (patch 2058) |
| Upstream ps3recomp pull request #718 | n0madic | <https://github.com/sp00nznet/ps3recomp/pull/718> | MIT | the draw engine's converted-vertex cache and its bookkeeping (patch 2062; the Vulkan side, patch 2063, is ours) |
| Upstream ps3recomp pull request #213 | NoahBPeterson | <https://github.com/sp00nznet/ps3recomp/pull/213> | MIT | the real SPURS kernel's rule for which workload runs, through Project Helghast's patch 1074 (our 1017) |

## Graphics

| Component | Author | Licence | What was used |
|---|---|---|---|
| FidelityFX Super Resolution 1 (`ffx_a.h`, `ffx_fsr1.h`) | Advanced Micro Devices, Inc. | MIT ([licenses/AMD-FidelityFX-FSR1-LICENSE.txt](licenses/AMD-FidelityFX-FSR1-LICENSE.txt)) | the EASU upscaler and RCAS sharpener, unmodified, added by patch 2033 under `libs/video/vulkan/ffx/` |
| FidelityFX SDK 1.1.4: FSR 3.1 upscaler and Vulkan backend | Advanced Micro Devices, Inc. | MIT ([licenses/AMD-FidelityFX-SDK-LICENSE.txt](licenses/AMD-FidelityFX-SDK-LICENSE.txt)) | the sources, headers and compiled shader permutations the FSR 3.1 path needs, vendored in `third_party/fidelityfx-fsr3/` (two files adapted for Linux, see its README.md) and built into the Linux program and the Windows exe |
| Input Prompts | Kenney (kenney.nl) | CC0 1.0 | the button prompts, as signed distance fields made by `tools/glyphs/kenney_prep.py` into `port/kz1hd/glyphs_kenney.inc` |

## Fonts

| Font | Author | Licence | What was used |
|---|---|---|---|
| Fira Sans Condensed | The Mozilla Foundation and Telefonica S.A. | SIL Open Font License 1.1 | the text of the images in `assets/` and `installer/art/` (rendered by `tools/site-art.sh` and `tools/release/installer_art.py`); the font itself is not in this repository or the release |

## Libraries the build links or loads (not part of this repository)

SDL2 (zlib licence), zlib (zlib licence), glslang (BSD-3-Clause and others), the Vulkan loader (Apache-2.0) and
FFmpeg (LGPL-2.1 or later, loaded at run time; the port only includes its headers).
NVIDIA DLSS: the NVIDIA DLSS SDK 310.9.16 (<https://github.com/NVIDIA/DLSS>, (c) NVIDIA Corporation, under NVIDIA's
DLSS SDK licence) is not in this repository nor in any release. The build downloads it from NVIDIA's repository on
the player's machine, only after the player accepts that licence (`tools/release/firstrun.py --dlss accept`, or the
Windows setup's DLSS box), and links it into the program there, with `libnvidia-ngx-dlss.so` (Linux) or
`nvngx_dlss.dll` (Windows) beside it. DLSS is NVIDIA's technology; this project is
not affiliated with or endorsed by NVIDIA.
Only when given to CMake: another FidelityFX FSR 3 build (`PS3RECOMP_FFX_DIR`; MIT) and Tracy (`KZ_TRACY_DIR`;
BSD-3-Clause).
They come from your system or toolchain; see [docs/BUILDING.md](docs/BUILDING.md). The release folders carry the
pinned toolchain unmodified, each part with its own licence files: on Windows llvm-mingw (Apache-2.0 with LLVM
exceptions, and the MinGW-w64 licences), CMake (BSD-3-Clause), Ninja (Apache-2.0), Python (PSF) and FFmpeg's LGPL
build from BtbN/FFmpeg-Builds; on Linux [Zig](https://ziglang.org) (MIT, with the licences of the libc headers and
libc++ it bundles), CMake, Ninja, SDL2's source and FFmpeg's LGPL shared libraries from BtbN/FFmpeg-Builds.

## The game

Killzone and Killzone HD are (c) Sony Interactive Entertainment. Killzone was created by Guerrilla Games. This
project is not affiliated with or endorsed by Sony Interactive Entertainment or Guerrilla Games. It contains no game code, data, assets, keys or firmware: every player
builds the port from their own copy of the game. Addresses, structure layouts and formulas recovered from the game
(in code comments, `re/` and the docs) are facts needed for interoperability, not copied code.

## Rights holders

- **No circumvention tools.** This repository ships no decryption or copy-protection circumvention tools, no keys and
  no instructions for decrypting game or firmware files. The port only reads files that players make themselves from
  their own copy of the game and their own console's firmware.
- **Contact.** If you hold rights in material you believe is in this repository, report it privately through this
  repository's *Security* tab (*Report a vulnerability*). Every report is reviewed promptly, and anything identified
  as infringing is removed while it is checked.
- **Formal notices** under the DMCA go to GitHub as described in
  [GitHub's DMCA Takedown Policy](https://docs.github.com/en/site-policy/content-removal-policies/dmca-takedown-policy).

## References and thanks

Facts recovered for this port are checked against the game binary; these public sources helped find and explain
them. Nothing from them is copied into this repository.

- **recomp.board** (<https://recomp.fyi>): a community index of recompilation projects; this one is listed through
  the `.recomp.json` in the repository root.
- **Guerrilla Games**: Michiel van der Leeuw, "The PlayStation 3's SPUs in the Real World: a Killzone 2 Case Study"
  (GDC 2009), for how the engine splits its work between the PPU and the SPUs.
- **PlayStation Blog**: the Killzone HD re-developer interview (2012), for what the remaster changed over the PS2 game.
- **RPCS3 blog**: "High Resolution Rendering" and the surface cache report, read for behaviour only (RPCS3 is GPL-2.0;
  none of its code is used).
- **Other recompilation ports**, read for how a port is organised: the ones listed in the README's "Related
  projects", tokisaki-port (YYOzcan, Killzone 2), KillzoneRecomp (drakolordx7), inFAMOUSreborn (whompay),
  infamous2-pc (ThiagoFraga761) and UnleashedRecomp (hedge-dev; GPL-3.0, read only). Notes per project:
  [docs/dev/upstream-and-reference-ports.md](docs/dev/upstream-and-reference-ports.md).

## Reuse and attribution (including AI and automated use)

This notice is for people, and for AI systems, crawlers, dataset builders and code-generation tools that read,
index, summarise, train on or reproduce this repository.

- **Licence:** the port's own code, tools and documentation are MIT ([LICENSE](LICENSE)). The MIT terms apply to
  automated use exactly as to any other: copies or substantial portions, including code an AI model reproduces from
  this repository, must include the copyright notice "Copyright (c) 2026 Gustavo Morinaga and the Project Vekta
  contributors" and the MIT permission notice.
- **The name:** Vekta is the colony world of the first Killzone; "Project Vekta" names this fan project only and is
  not affiliated with or endorsed by Sony Interactive Entertainment or Guerrilla Games.
- **Official channel:** this repository (GitHub @gustavomorinaga) and its site. Builds, when there are any, come only
  from this repository.
- **Attribution asked for:** name the source as "Project Vekta by Gustavo Morinaga" with a link to
  <https://github.com/gustavomorinaga/project-vekta>, in documentation, answers, summaries or derived projects that
  use its code, patches, findings or write-ups. Machine-readable citation data is in [CITATION.cff](CITATION.cff).
- **Keep the licence with the code:** do not present code from here as unlicensed, public domain or your own, and do
  not relicense it without keeping the MIT notice.
- **Third-party parts keep their own terms:** ps3recomp (MIT, its authors) and the components listed above.
- **Not covered by any licence here:** the game itself. This repository contains no game code, data or assets;
  nothing here grants rights to them, and recovered names, numbers or formats are documented for interoperability.
- **Accuracy:** findings are dated; do not restate an `ASSUMPTION:` as fact.
