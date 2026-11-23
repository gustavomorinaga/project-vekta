# Starting from a game launcher

How the release folders meet the launchers that take a downloaded package and start it, and what to ask those
launchers for. The player-facing steps are in [INSTALL.md](../INSTALL.md), "Installing through a launcher"; the
options and exit codes in [BUILDING.md](../BUILDING.md), "Starting from a launcher". Findings dated 2026-11-20.

## The contract

- **One program at the top.** The Windows folder (`project-vekta-X.Y.Z-windows.zip`) has `Project Vekta.exe` and
  the Linux folder (`project-vekta-X.Y.Z-linux.tar.xz`) has `project-vekta`, and no other startable file at the
  top: on Windows no other `.exe`, `.bat` or `.cmd`; on Linux no `.sh`, no file starting with `#!/` and no ELF file
  (`.txt`, `.dll`, `.so` and `.json` do not count). `tools/release/assemble_payload.py` refuses a folder that breaks
  this (`layout_problems()`), and `tools/tests/test_payload.py` checks both folders. The scripts are in `scripts/`.
- **No arguments, from its own folder.** Either program plays. Before the first build it checks the files and builds
  first (`tools/release/launch.py --build-if-needed`), asking nothing when RPCS3 and the game are in their usual
  places. It needs no `HOME`, `USER` or `XDG_RUNTIME_DIR` (`project-vekta` changes to its own folder).
- **The player's files:** `--game DIR` (the installed NPUA80856 folder, with `PARAM.SFO` and `USRDIR`), `--eboot FILE`
  (a decrypted `EBOOT.elf`, SHA-256 `c81aa91e35a76ff7d856095c392cee98bcc4b01879d7f13e5a389c6ee2d903fc`), `--psarc
  FILE`, `--rpcs3 [DIR]`, `--disc PATH` (the disc with no RPCS3 installed), or `KZ_GAME`, `KZ_EBOOT`, `KZ_PSARC`. They are kept in `work/paths.env` once the check
  passes.
- **The prepare step:** `scripts/prepare.cmd` and `scripts/prepare.sh` (`launch.py --prepare`). No window and no
  question; `progress <0..1> <text>` lines; on a failure one `reason <text>` line at the end; exit 0 when built.
  `--timeout SECS` stops it cleanly (the next run resumes). `--dlss accept` is the player's answer to NVIDIA's DLSS
  SDK licence, never given for them.
- **The exit codes:** one table, `port/kz1hd/kz_exit.h`, for the exe and the launcher; `launch.py` reads it, and
  [TROUBLESHOOTING.md](../TROUBLESHOOTING.md) is built around it.
- **The Linux archive is a `.tar.xz` from 0.7.0** (a `.tar.gz` before). RecompLauncher's developer guide
  (`docs/DEVELOPERS.md` at `0f4540e`, Beta 67) names the kind: on Linux a `.tar.gz`, `.tar.xz` or `.tar.zst`, picked
  by the system's name in the file's name, with `source` and `.sha256` files never taken. Its source takes the three
  alike (`src-tauri/src/assets.rs`), and Quiver 3.5.1 and 4.0 unpack `.tar.xz`. The guide also says that a listing
  which names the file falls back to reading the names after a rename. In the Beta 67 source that holds for a system
  the listing does not name: where the listing's pattern for Linux matches no file of a release (a name ending in
  `-linux.tar.gz`), the launcher installs the newest release that has a match, so Linux players would stay on 0.6.0
  until the listing is updated. Neither catalog's entry for this port was read (RecompLauncher's `assets`, Quiver's
  `releaseAssetFilter`, one filter for every system's files): after the first `.tar.xz` release, see that both
  launchers offer it on Linux.
- **`recomplauncher.json`** at the top of both folders (`tools/release/recomplauncher.py`, from
  `installer/recomplauncher.json`): the same contract in RecompLauncher's format 1. Its inputs are the game folder
  or the disc (role `disc`, `--disc={input:disc}`), both optional, and the decrypted EBOOT.
- **Not yet:** the launchers' mode hints (`RECOMPLAUNCHER_MODE=bigpicture|desktop`, `RECOMPLAUNCHER_PAD=xbox|
  playstation`, Steam Deck Game Mode) are not read; planned for 0.7.0 (full screen in Big Picture mode, the button
  icons of the pad that is plugged in).

## Quiver

Stable Quiver (3.5.1) starts the one startable file it finds at the top of the package: the rule above is all it
needs, and nothing else is set for it.

The values to request for its 4.0 beta catalog entry, which only matter there:

| Field | Value |
|---|---|
| `preferredExecutables` | `Project Vekta.exe` (Windows), `project-vekta` (Linux) |
| `setupFiles` | the game folder: a folder that contains `PARAM.SFO` and `USRDIR`, passed as `--game <folder>`; and, optional, `EBOOT.elf`, passed as `--eboot <file>`, SHA-256 `c81aa91e35a76ff7d856095c392cee98bcc4b01879d7f13e5a389c6ee2d903fc` |
| `saves.locations` | rooted at the app folder: `work/vfs/gamedata`, `work/vfs/kz1hd_pc.ini`, `work/vfs/kz1hd_keys.ini` |

No firmware file is asked for. Nothing has been sent to Quiver's maintainers; the request is the maintainer's to
make.

## RecompLauncher

`recomplauncher.json` (format 1) names the game folder input (role `game`), the optional `EBOOT.elf`, the prepare
step with its DLSS tick and its `done` file (`work/build-stamp.json`, which the first run writes only after a good build, so the launcher knows the game is built), the program to start, the saves, the log folders and the exit codes. The format was read
from RecompLauncher's developer guide; `tools/release/recomplauncher.schema.json` is written here (RecompLauncher's
own schema is GPL-3.0 and is not copied). Before a listing: a local "Add a build" test with a release dry run's
folder.

## Checking a change

- `python3 tools/release/recomplauncher.py --check`, `tools/tests/test_launcher_contract.py`,
  `tools/tests/test_recomplauncher.py`, `tools/tests/test_payload.py`.
- A folder with the release layout started with `env -i PATH=/usr/bin:/bin ./project-vekta` (Linux) and
  `wine "Project Vekta.exe"` (Windows), as in the pull request that added this contract.
