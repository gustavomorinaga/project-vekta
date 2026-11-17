# Third-party downloads served from this repository

The build downloads its toolchain and libraries from the places `tools/release/toolchain.json` names, each checked
against its SHA-256. Most of those places keep old versions for good. BtbN/FFmpeg-Builds does not: it removes most
dated builds after a while (keeping each month's last), and when the pinned one went away, 0.5.0's release could
not be built. So the FFmpeg the port uses is served from a release of this repository instead.

## How it works

- An entry with an `upstream_url` (today `ffmpeg` and `ffmpeg-linux`) has its `url` under this repository's
  releases: `https://github.com/gustavomorinaga/project-vekta/releases/download/deps-ffmpeg-{version}/<file>`.
  `upstream_url` is where the file came from, under the same file name. `fetch_toolchain.py --check` refuses an
  entry that breaks either rule.
- The release `deps-ffmpeg-<version>` holds the upstream files byte for byte, a `.sha256` beside each, and FFmpeg's
  corresponding source for the LGPL (`ffmpeg-<version>-source.tar.xz`, a `git archive` of the commit the build
  names, at the end of its version: `n9.0.2-57-gd838f9f9ae` is commit `d838f9f9ae`). It is a pre-release that is
  never marked "latest", so the releases page and the website still point at the port's own versions;
  `release-check.yml` skips `deps-*` tags.
- The SHA-256 the build checks is the one in `toolchain.json`, the same as before: the file is the one the
  maintainer tested, wherever it is served from.

## Moving the FFmpeg pin

1. In `toolchain.json`, change `version`, `upstream_url` and `sha256` of both `ffmpeg` and `ffmpeg-linux`
   (`fetch_toolchain.py --only ffmpeg --fill` with the sha256 set to `TO-FILL` writes it from the download; compare
   it with the release page). The `url` follows `version`, so the new pin is a new tag: a published tag or asset is
   never replaced.
2. Merge it, then run the **deps-mirror** workflow (Actions, "Run workflow") from `main`: first without `publish`
   (a dry run: it downloads, checks the pins and builds the source archive as an artifact), then with `publish`. It
   creates `deps-ffmpeg-<version>` at that commit and uploads what the release does not have yet; an asset already
   there is checked against the pin and kept.
3. Run `release.yml`'s dry run (workflow_dispatch) before tagging a version that uses it.

The first release, `deps-ffmpeg-n9.0.2-57-gd838f9f9ae` (BtbN's autobuild-2026-11-16-15-29, the 0.5.1 pin), was
published by hand with the two builds and their `.sha256` (hashes equal to the pins), and the workflow then added the
source archive.
