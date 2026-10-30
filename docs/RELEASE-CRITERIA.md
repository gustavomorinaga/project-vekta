# Release criteria

Project Vekta has no release and no release date. Versions below 1.0.0 are previews: **v0.x = preview**, published
as GitHub pre-releases, each saying what is missing. **v1.0.0 = the whole campaign playable start to finish, on Linux
and on Windows, at a steady 60 frames per second on the default settings.** This list, not a date, is what a release
is measured against. The first preview will be v0.1.0.

## Gates for a v0.x tag (preview)

1. **CI green** on the commit that is tagged.
2. **A build anyone can repeat:** the tagged commit builds from the player's own files with the steps in
   [BUILDING.md](BUILDING.md), on a machine other than the maintainer's.
3. **Known issues listed** in [KNOWN_ISSUES.md](KNOWN_ISSUES.md), and a `CHANGELOG.md` section for the version.
4. **One version string** in the tag, `CHANGELOG.md` and `CITATION.cff`.
5. **SHA256SUMS** published beside any file the release carries.
6. **Tags are never moved**; a fix is a new tag.
7. **Published as a pre-release** while the version is 0.x.

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
