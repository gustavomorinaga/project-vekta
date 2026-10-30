# Changelog

What each release contains, newest first, in the style of [Keep a Changelog](https://keepachangelog.com/). There is
no release yet: the port is built from source. Until a release that calls itself playable
([docs/RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md)), every version is a pre-release.

## [Unreleased]

### Added
- The game's own MLAA anti-aliasing, reimplemented as a GPU compute pass equal to the SPU code (patches 2035 to 2037,
  `KZ_MLAA=gpu|spu|off`); the first frame no longer waits for the driver to compile it.
- NVIDIA Reflex (Off, On, On + Boost) on the PC page, through `VK_NV_low_latency2` (patch 2038).
- In-game key capture for remapping screens (patch 4011).
- `tools/wine-test.sh` runs the Windows build under Wine or Proton for a minute and says whether it held its frame
  rate; it passed at 144 fps under both on the maintainer's machine.
- `tools/mlaa_harness` and `tools/mlaa_probe`: the lifted MLAA jobs run outside the game, test patterns and checks.
- Releases: `VERSION` holds the one version string; `tools/release/bump_version.py` moves it everywhere and turns
  this section into the release's entry; a pushed tag builds a reproducible source archive (the tree with ps3recomp
  at its pinned commit) and opens a draft pre-release with notes from this file and SHA-256 sums
  ([docs/RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md)).

### Changed
- The project page is generated whole from the repository (the feature table, the screenshots, this changelog,
  the requirements and the patch series) and laid out like the Killzone 2 and Killzone 3 ports' pages;
  `docs/FEATURES.md` gains a section on the port itself (builds, frame rate, installer).

### Fixed
- `tools/ps3recomp-patched.sh` also works in an unpacked source archive, where there is no git checkout.
- An SPU write into a GPU-mapped range that overlaps one already unmapped is seen as GPU-mapped again (patch 2039),
  and the SPU poison-write diagnostic works with `SPU_POISON_DMA_WATCH` (patch 1019); both found by ps3recomp's own
  tests, which now run on the patched tree in CI (patch 0004).
- An SPU that reserves a line while another SPU's plain write to it is being copied now loses the reservation at once,
  not at the next 10 ms poll (patch 1020).

## [First public tree] - 2026-10-30

### Added
- The port of Killzone HD (NPUA80856) on ps3recomp v0.18.0 with 93 patches, built on Linux, and on Linux for Windows.
- Scripts that build it from the player's own files with no fixed paths (`tools/`), a checker for those files, and
  the function seeds of the lift as a table (`re/function_seeds.txt`).
- PC features: the in-game PC page (display, resolution, internal scale, V-Sync, frame-rate limit 30/60/144, FSR 1,
  anisotropic filtering, widescreen, field of view, HUD, presets, controllers, mouse, sound output, language, skip
  intro), direct mouse look, mouse in menus, remappable keys, device-aware button prompts, local trophies.
- Documentation in English: README, getting files, building, how it works, features, known issues, roadmap, and the
  engineering notes in `docs/dev/`.
