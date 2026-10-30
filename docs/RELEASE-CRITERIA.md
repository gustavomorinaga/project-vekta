# Release criteria

Project Vekta has no release and no release date. Versions below 1.0.0 are previews: **v0.x = preview**, published
as GitHub pre-releases, each saying what is missing. **v1.0.0 = the whole campaign playable start to finish, on Linux
and on Windows, at a steady 60 frames per second on the default settings.** This list, not a date, is what a release
is measured against. The first preview will be v0.1.0.

## Gates for a v0.x tag (preview)

1. **CI green** on the commit that is tagged.
2. **A build anyone can repeat:** the tagged commit builds from the player's own files with the steps in
   [BUILDING.md](BUILDING.md), on a machine other than the maintainer's. **Relaxed for v0.1.0 only** (the
   maintainer's decision, 2026-10-30): the first preview may ship when the tagged release's own source archive,
   unpacked in a fresh folder, builds and plays on the maintainer's machine; its release notes say that no other
   machine has built it yet. From v0.1.1 on the gate applies as written.
3. **Known issues listed** in [KNOWN_ISSUES.md](KNOWN_ISSUES.md), and a `CHANGELOG.md` section for the version.
4. **One version string** in the tag, `CHANGELOG.md` and `CITATION.cff`.
5. **SHA256SUMS** published beside any file the release carries.
6. **Tags are never moved**; a fix is a new tag.
7. **Published as a pre-release** while the version is 0.x.

## How a release is made

A release is the source for now: `project-vekta-X.Y.Z-source.tar.gz` (the tagged tree with ps3recomp at its pinned
commit, made by `tools/release/pack_source.py`, the same bytes from the same commit), its `.sha256` and `SHA256SUMS`.
There is no installer yet; the archive builds the port as [BUILDING.md](BUILDING.md) says.

1. With the gates above met, on an up-to-date `main`: `python3 tools/release/bump_version.py X.Y.Z`. It writes
   `VERSION`, `CITATION.cff`, the README's release link and the `CHANGELOG.md` entry (from `## [Unreleased]`),
   regenerates the project page and checks the version mentions and the release notes.
2. Commit that ("Release vX.Y.Z"), merge it through a pull request, then tag the merge commit `vX.Y.Z` and push the tag.
3. `.github/workflows/release.yml` checks the tag against `VERSION`, writes the notes from the `CHANGELOG.md` entry,
   builds the archive and opens a **draft** pre-release with it. Read the draft and publish it by hand.
4. Publishing runs `.github/workflows/release-check.yml`, which checks the published release against the tag
   (`tools/release/release_check.py`): title, pre-release flag, the three files, their sums, and the links.

A dry run (Actions, release, "Run workflow") builds the archive and previews the notes without publishing anything.

## Release criteria for v1.0.0 (playable)

1. **The whole campaign** (levels 1 to 11) played from start to end on Linux and on Windows, each level checked
   against reference footage of the PS3 game, with no crash, hang or missing effect that changes play.
2. **Saves** made at checkpoints load back and continue, end to end.
3. **An installer or first-run build** that checks the player's own files and builds the port on their machine, on
   Linux and on Windows, without a developer setup.
4. **60 fps steady** on the default settings on the tested hardware, and the known issues of the 144 Hz mode either
   fixed or the mode marked experimental.
5. **Tested on more than one machine**: at least one clean Windows install and one GPU vendor other than NVIDIA.
6. **The docs match the release:** features, known issues, requirements and getting your files.
7. **Accurate label:** the README and release notes say what is translated, what is reimplemented and what is native,
   and how AI was used (README, "How AI was used").
8. **No emulator tells:** release builds expose none of the runtime's debug UI, hotkeys, overlays or diagnostic
   switches by default; only the port's own settings, styled like the game.

The numbers here are targets, not a statement about today: see the README's [Status](../README.md#status) table for
what works now.
