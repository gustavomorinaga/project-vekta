# Changelog

What each release contains, newest first. Until a release that calls itself playable
([docs/ROADMAP.md](docs/ROADMAP.md)), every version is a pre-release.

## Unreleased: the first public tree (2026-10-30)

- The port of Killzone HD (NPUA80856) on ps3recomp v0.18.0 with 93 patches, built on Linux, and on Linux for Windows.
- Scripts that build it from the player's own files with no fixed paths (`tools/`), a checker for those files, and
  the function seeds of the lift as a table (`re/function_seeds.txt`).
- PC features: the in-game PC page (display, resolution, internal scale, V-Sync, frame-rate limit 30/60/144, FSR 1,
  anisotropic filtering, widescreen, field of view, HUD, presets, controllers, mouse, sound output, language, skip
  intro), direct mouse look, mouse in menus, remappable keys, device-aware button prompts, local trophies.
- Documentation in English: README, getting files, building, how it works, features, known issues, roadmap, and the
  engineering notes in `docs/dev/`.
