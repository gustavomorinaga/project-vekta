# Third-party components and credits

Every piece of code, patch, idea or text that came from outside this repository is listed here with its author,
source, licence and what was used. Licence texts are in [licenses/](licenses/). Per-patch credit is also in each
patch's header in `port/patches/`. The port's own code is MIT ([LICENSE](LICENSE)).

## Toolkit

| Project | Author | Source | Licence | What was used |
|---|---|---|---|---|
| ps3recomp | Ned Heller (sp00nznet) and its contributors | <https://github.com/sp00nznet/ps3recomp> (submodule `third_party/ps3recomp`, pinned at release v0.18.0) | MIT ([licenses/ps3recomp-LICENSE.txt](licenses/ps3recomp-LICENSE.txt)) | the PPU and SPU lifters, the PS3 runtime and the HLE system libraries the port is built on; our changes are the patches in `port/patches/`, applied on top of that exact release |

## Other ports

| Project | Author | Source | Licence | What was used |
|---|---|---|---|---|
| Project Helghast (Killzone 3 PC port) | undecuple | <https://github.com/undecuple/project-helghast> | MIT ([licenses/Project-Helghast-LICENSE.txt](licenses/Project-Helghast-LICENSE.txt)) | the SPURS kernel's admission rule and `cellSpursRequestIdleSpu` as its patch 1074 ports them (our 1017); the policy-module `selectWorkload` answer of its patch 1058 (our 1018); the MinGW sampler stack read and `ppu_resv_break` linkage of its patch 0002 (our 0003); `tools/ps3recomp-patched.sh` (line-ending check, Python lookup, content-only sync); the repository layout, the patch number blocks and the shape of `AGENTS.md`, `CONTRIBUTING.md` and the docs; the shape of `tools/run-host-tests.sh`, running ps3recomp's own tests on the patched tree (`tools/tests/test_upstream_suite.sh`), the shape of `tools/wine-test.sh` (after its `tools/proton-test.sh`), the `ruff.toml` rule set, `.gitleaks.toml`, `docs/features.tsv` with `tools/features.py`, the rule of `tools/sandbox.sh`, and the CodeQL, issue-label and pages workflows |
| Project Pyrrhus (Killzone 2 PC port) | undecuple | <https://github.com/undecuple/project-pyrrhus> | MIT ([licenses/Project-Pyrrhus-LICENSE.txt](licenses/Project-Pyrrhus-LICENSE.txt)) | the shape of the public documentation (README sections, getting-files and release-criteria pages) and of the project site, its generated page and stylesheet (`tools/website.py` and `site/style.css`, after its `tools/release/website.py` and `site/style.css`); the release tools and workflows (`tools/release/`, `release.yml`, `release-check.yml`, `tools/tests/test_version_agree.py`, after theirs and Project Helghast's); the first-run builder (`tools/release/firstrun.py`: its steps table, markers, progress and status lines, job count and log, after theirs and Project Helghast's) |
| Upstream ps3recomp pull request #213 | NoahBPeterson | <https://github.com/sp00nznet/ps3recomp/pull/213> | MIT | the real SPURS kernel's rule for which workload runs, through Project Helghast's patch 1074 (our 1017) |

## Graphics

| Component | Author | Licence | What was used |
|---|---|---|---|
| FidelityFX Super Resolution 1 (`ffx_a.h`, `ffx_fsr1.h`) | Advanced Micro Devices, Inc. | MIT ([licenses/AMD-FidelityFX-FSR1-LICENSE.txt](licenses/AMD-FidelityFX-FSR1-LICENSE.txt)) | the EASU upscaler and RCAS sharpener, unmodified, added by patch 2033 under `libs/video/vulkan/ffx/` |

## Libraries the build links or loads (not part of this repository)

SDL2 (zlib licence), zlib (zlib licence), glslang (BSD-3-Clause and others), the Vulkan loader (Apache-2.0) and
FFmpeg (LGPL-2.1 or later, loaded at run time from the player's own copy; the port only includes its headers).
They come from your system or toolchain; see [docs/BUILDING.md](docs/BUILDING.md).

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
