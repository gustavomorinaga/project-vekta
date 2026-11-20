# Getting your files ready

The port is built from your own copy of the game. This page says which files the scripts need, where they look for
them, and how they are checked. It does not explain how to decrypt anything: this project ships no decryption tools
or keys and gives no such instructions. The files below come from your own disc and your own PS3 firmware.

## What you need

| File | What it is | Where the scripts look (`tools/env.sh`) | Required |
|---|---|---|---|
| The installed game folder | Killzone HD, title **NPUA80856**, as installed from the `PS3_EXTRA` package of the **Killzone Trilogy** disc (US, BCUS98116) or bought on the US PlayStation Store: `PARAM.SFO`, `TROPDIR/`, `USRDIR/` | `Game/NPUA80856/` (`KZ_GAME`) | yes |
| `EBOOT.elf` | the game's executable, decrypted: an ELF file, not the `EBOOT.BIN` container in `USRDIR` | `Game/EBOOT.elf` (`KZ_EBOOT`) | yes |
| `data_ps3.psarc` | the game's data archive, decrypted: a PSARC file. Without it the port reads the installed game's own `USRDIR/data_ps3.psarc.edat` (see below) | `Game/data_ps3.psarc` (`KZ_PSARC`) | no |
| `dev_flash` | an installed PS3 system software 4.93, as RPCS3 keeps it; the port reads `sys/external/flashMP3.pic` from it as it is | `~/.config/rpcs3/dev_flash`; on Windows `Game\dev_flash`, since RPCS3 keeps it in its own folder, beside `rpcs3.exe` (`PS3_DEV_FLASH`) | for music and sound |

Why the firmware: the game's music decoder is loaded from `flashMP3.pic`. The port runs that code, translated like
the game's, instead of rewriting it. Up to 0.5.1 the port also needed the firmware's `libsre.prx`, decrypted, for the
SPURS queue through which the game sends work to an SPU task; from 0.6.0 the port's runtime does that itself
(ps3recomp patch 1022), and a `libsre.prx` saved by an earlier install is no longer read.

**The data archive without preparing it.** When there is no decrypted `data_ps3.psarc`, the port uses the
`data_ps3.psarc.edat` in the installed game's `USRDIR`, as the package or RPCS3 installs it. The game itself passes
the archive's key to the system when it starts, as on a PS3; the port's runtime (ps3recomp's EDAT reader, with patch
4014) uses it to open the archive and keeps the opened copy in `work/edat-cache` (3.1 GB). The game folder is only
read. The install does this once after the build (`tools/kzbuild.py data`: the game runs without a window until the
archive is open, about a minute), checks the result against the `data_ps3.psarc` row below, and takes the movies out
of it. No key is in this repository or in the downloads.

**From your RPCS3.** `tools/install.sh`, the release folders' `Project Vekta.exe` and `./project-vekta` (with no question), `scripts\run.cmd` and
`scripts/run.sh` (or `tools/release/firstrun.py --rpcs3 [DIR]`) find
RPCS3 in its usual places, take the installed game folder, its `.edat` and `dev_flash` from it, and ask your RPCS3 to
decrypt a copy of `USRDIR/EBOOT.BIN` into `work/prep` (`tools/release/rpcs3_import.py`). The result must match the
`EBOOT.elf` row below. RPCS3's
settings for that run go to `work/prep` too; your game and RPCS3 folders are only read.

**The PlayStation Store copy.** The Store sells the same Killzone HD (NPUA80856, content ID
`UP9000-NPUA80856_00-GKILLZONE1000001`). Its `EBOOT.BIN` and `data_ps3.psarc.edat` differ from the disc package's,
but they decrypt to the same `EBOOT.elf` and `data_ps3.psarc` (measured on 2026-11-19). Its `.edat` is bound to the
buyer's licence (licence type 2 in its header, where the disc package's says 3), so the port's own reader cannot
open it. With "Use my RPCS3 install", your RPCS3, which already has the game's licence (`.rap`) to run it, decrypts a
copy of the `.edat` into `work/prep/data_ps3.psarc` (it must match the `data_ps3.psarc` row below; about 3 GB more
in `work`). On the same drive as the game the copy is a hard link, so no second 3 GB file is written; on another
drive it is a real copy for the length of the step. Without RPCS3 the check names the Store copy and stops.

The European edition (NPEA00418, and the disc BCES-01743) is not supported: its executable is another build.

You can keep the files anywhere: set the variables in the third column to point at them, or link them into `Game/`.
`Game/` and `work/` are ignored by git, so nothing you put there can be committed by accident.

## Checking them

```sh
tools/check-game-files.py          # size, type and SHA-256 of each file
tools/check-game-files.py --quick  # skips hashing the 3 GB archive
```

Each line says `ok`, `MISSING`, `WRONG` (not the right kind of file: for example the `.edat` given as the
decrypted archive, or `EBOOT.BIN` instead of `EBOOT.elf`) or `DIFFERENT` (the right kind of file, but not the version
the port was made from). With no decrypted archive the check looks at the game's `data_ps3.psarc.edat` instead. During the first
run it also checks the folder the port is in (`--root`, `--work`): no `[`, `]` or `;` in its path, not inside
OneDrive, and a PC clock not behind the port's files. When `dev_flash` is there, its system software version
(`vsh/etc/version.txt`) must be 4.93: another one stops the check, and the setup's firmware page and the RPCS3 route
refuse it too, naming the version they found. A required file that is missing, wrong or different stops the check, and the build with it;
an optional one (the trophy set, `flashMP3.pic`) only gets its line. The tested versions:

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `PARAM.SFO` | 1,040 | `352712c456e94be80fca0cd46dda271b96e81d7bf39365cf13448415dca6f642` |
| `TROPDIR/NPWR03819_00/TROPHY.TRP` | | `6a770bcec111b64c66a5f674f2107b8957334845d4876f4e8dbcfc93bbc10858` |
| `EBOOT.elf` | 19,706,080 | `c81aa91e35a76ff7d856095c392cee98bcc4b01879d7f13e5a389c6ee2d903fc` |
| `data_ps3.psarc` (also what the `.edat` opens to) | 3,147,174,243 | `341f3c61ee14836047659ebca863ab454fff14f712301ccf8e7f9029deb65d2d` |
| `USRDIR/data_ps3.psarc.edat` | 3,153,321,376 | `3c85c5321bccab33079896f5e74c06902e84648154b315af21e14878d93fe58b` |
| `USRDIR/data_ps3.psarc.edat` (PlayStation Store copy; your RPCS3 opens it) | 3,153,321,376 | `c130493bc0d9b32bf00aa42e4e24cce1187c76f802838929267ad3255241962a` |
| `flashMP3.pic` (4.93) | 69,695 | `adbd993483a41f53c994e080665fb5fbf36af783956d0e4ded30057b8b4e894a` |

The lift has addresses of this EBOOT built in (`tools/lift.sh`, `re/function_seeds.txt`, the native functions in
`port/kz1hd/`), so a different EBOOT will not work. The music decoder from another system software version has
not been tried, so the check stops on a `dev_flash` other than 4.93 (`ASSUMPTION:` its code is unchanged across
late versions, so another one may work, but nobody has tried).

## Then

[docs/BUILDING.md](BUILDING.md): patch ps3recomp, lift, set up the run folder, build and play.
