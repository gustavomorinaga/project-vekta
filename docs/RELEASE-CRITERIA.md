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
   machine has built it yet. **Relaxed again for v0.2.0**, the first release with the Windows setup program (the
   maintainer's decision, 2026-11-01): it was installed, built and played on the maintainer's machine, on Linux and on
   Windows; its notes say so and ask for reports. From v0.2.1 on the gate applies as written.
3. **Known issues listed** in [KNOWN_ISSUES.md](KNOWN_ISSUES.md), and a `CHANGELOG.md` section for the version.
4. **One version string** in the tag, `CHANGELOG.md` and `CITATION.cff`.
5. **SHA256SUMS** published beside any file the release carries.
6. **Tags are never moved**; a fix is a new tag.
7. **Published as a pre-release** while the version is 0.x.

## How a release is made

A release carries four files (three up to 0.2.0, which had no Linux folder), each with a `.sha256`, and `SHA256SUMS`. None holds anything of the game; each builds
the port from the player's own files ([BUILDING.md](BUILDING.md)).

- `project-vekta-X.Y.Z-source.tar.gz`: the tagged tree with ps3recomp at its pinned commit
  (`tools/release/pack_source.py`, the same bytes from the same commit).
- `project-vekta-X.Y.Z-windows.zip`: the Windows folder (`tools/release/assemble_payload.py --zip`, also the same
  bytes from the same commit): the sources, the patched ps3recomp, the pinned toolchain and the launcher.
- `project-vekta-X.Y.Z-setup.exe`: that folder in the setup program (`tools/release/build_setup.py`, Inno Setup).
- `project-vekta-X.Y.Z-linux.tar.gz`: the Linux folder (`tools/release/assemble_payload.py --platform linux --tar`,
  the same bytes from the same commit): the sources, the patched ps3recomp and the pinned Zig toolchain.

1. With the gates above met, on an up-to-date `main`: `python3 tools/release/bump_version.py X.Y.Z`. It writes
   `VERSION`, `CITATION.cff`, the README's release link and the `CHANGELOG.md` entry (from `## [Unreleased]`),
   regenerates the project page and checks the version mentions and the release notes.
2. Commit that ("Release vX.Y.Z"), merge it through a pull request, then tag the merge commit `vX.Y.Z` and push the tag.
3. `.github/workflows/release.yml` checks the tag against `VERSION`, writes the notes from the `CHANGELOG.md` entry,
   builds the four files, installs and uninstalls the setup program on a Windows runner, builds the Linux folder's
   toolchain in a Debian 11 container with no compiler, and opens a **draft**
   pre-release with them. Read the draft and publish it by hand.
4. Publishing runs `.github/workflows/release-check.yml`, which checks the published release against the tag
   (`tools/release/release_check.py`): title, pre-release flag, the files, their sums, and the links.

A dry run (Actions, release, "Run workflow") builds the files and previews the notes without publishing anything.

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
