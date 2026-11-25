# Getting your files ready

The port is built from your own copy of the game. This page says which files the scripts need, where they look for
them, and how they are checked. It does not explain how to decrypt anything: this project ships no decryption tools
or keys and gives no such instructions. The files below come from your own disc; no PS3 system software is needed.

## What you need

| File | What it is | Where the scripts look (`tools/env.sh`) | Required |
|---|---|---|---|
| The installed game folder | Killzone HD, title **NPUA80856**, as installed from the `PS3_EXTRA` package of the **Killzone Trilogy** disc (US, BCUS98116) or bought on the US PlayStation Store; or the European edition, **NPEA00418**, from the European PlayStation Store, or the `PS3_GAME` folder of a copy of the European disc, **BCES-01743** (see below): `PARAM.SFO`, `TROPDIR/`, `USRDIR/` | `Game/NPUA80856/` (`KZ_GAME`) | yes |
| `EBOOT.elf` | the game's executable, decrypted: an ELF file, not the `EBOOT.BIN` container in `USRDIR` | `Game/EBOOT.elf` (`KZ_EBOOT`) | yes |
| `data_ps3.psarc` | the game's data archive, decrypted: a PSARC file. Without it the port reads the installed game's own `USRDIR/data_ps3.psarc.edat` (see below) | `Game/data_ps3.psarc` (`KZ_PSARC`) | no |

No firmware: up to 0.5.x the port also needed two pieces of PS3 system software 4.93, the decrypted `libsre.prx` (the
SPURS queue through which the game sends work to an SPU task) and `dev_flash`'s `flashMP3.pic` (the music decoder).
From 0.6.0 the port's runtime does the queue itself (ps3recomp patch 1022) and decodes the music with minimp3; the
game still asks for `flashMP3.pic`, and the port gives it a stand-in it writes itself, with no code in it
([dev/flashmp3-native.md](dev/flashmp3-native.md)). A `libsre.prx` or `dev_flash` saved by an earlier install is no
longer read.

**The data archive without preparing it.** When there is no decrypted `data_ps3.psarc`, the port uses the
`data_ps3.psarc.edat` in the installed game's `USRDIR`, as the package or RPCS3 installs it. The game itself passes
the archive's key to the system when it starts, as on a PS3; the port's runtime (ps3recomp's EDAT reader, with patch
4014) uses it to open the archive and keeps the opened copy in `work/edat-cache` (3.1 GB). The game folder is only
read. The install does this once after the build (`tools/kzbuild.py data`: the game runs without a window until the
archive is open, about a minute), checks the result against the `data_ps3.psarc` row below, and takes the movies out
of it. No key is in this repository or in the downloads.

**From your RPCS3.** `tools/install.sh`, the release folders' `Project Vekta.exe` and `./project-vekta` (with no question), `scripts\run.cmd` and
`scripts/run.sh` (or `tools/release/firstrun.py --rpcs3 [DIR]`) find
RPCS3 in its usual places, take the installed game folder, its `.edat` from it, and ask your RPCS3 to
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

**The European edition.** Killzone HD NPEA00418 (content ID `EP9000-NPEA00418_00-GKILLZONE1000001`, the European
PlayStation Store's; its archive has English, French, German, Italian, Spanish and Dutch text) is the same program as
NPUA80856 built again, its code moved by up to 232 bytes. The build finds the edition in the game folder's `PARAM.SFO`
and lifts it with the addresses of `re/eu/address_map.tsv` ([dev/editions.md](dev/editions.md)); its saves go to
`savedata/NPEA00418`. Its files are checked against the second table below. Install it in your RPCS3 with its licence
and choose "Use my RPCS3 install", as for the US Store copy.

**The European disc.** Killzone HD BCES-01743 (the European "Classics HD" disc, also in the European Killzone
Trilogy) holds the game itself, not a package. A copy of the disc's folder (`PS3_DISC.SFB` and `PS3_GAME/`) whose
files read as the game reads them has the European edition's program and archive, byte for byte: its `EBOOT.BIN`
decrypts to the `EBOOT.elf` of the second table and its `PS3_GAME/USRDIR/data_ps3.psarc` is that table's
`data_ps3.psarc` (measured on 2026-11-25). Give the disc copy's folder to the disc route below (`--disc FOLDER`, or
"From my Killzone Trilogy disc" in the setup): nothing is installed, the official RPCS3 decrypts a copy of
`EBOOT.BIN` into `work/prep`, and the port reads the archive where it is (the folder is only read). A raw image of the
disc, whose files are still encrypted as they are on the disc, is named and refused. Its `PARAM.SFO` is checked by
its fields (title `BCES01743`, version and app version 01.00), as a copy of the disc may change other bytes of it.

You can keep the files anywhere: set the variables in the third column to point at them, or link them into `Game/`.
`Game/` and `work/` are ignored by git, so nothing you put there can be committed by accident.

**From the disc, with no RPCS3 installed.** `--disc FOLDER` (the copied disc, or its `PS3_EXTRA` package) has the
official RPCS3, downloaded at a pinned version, install the package into `work/prep/disc` and decrypt `EBOOT.BIN`
into `work/prep` ([INSTALL.md](INSTALL.md); `tools/release/rpcs3_disc.py`). Only the disc's package is taken
(3,182,645,568 bytes, SHA-256 `94e8db935dca69b5c8b46f21196a923da957fcaf123c895c51cdc6dde4f50179`); the installed files
are checked against the table below. A copy of the European disc is taken from its folder too (see above).

## Checking them

```sh
tools/check-game-files.py          # size, type and SHA-256 of each file
tools/check-game-files.py --quick  # skips hashing the 3 GB archive
```

Each line says `ok`, `MISSING`, `WRONG` (not the right kind of file: for example the `.edat` given as the
decrypted archive, or `EBOOT.BIN` instead of `EBOOT.elf`) or `DIFFERENT` (the right kind of file, but not the version
the port was made from). With no decrypted archive the check looks at the game's `data_ps3.psarc.edat` instead. During the first
run it also checks the folder the port is in (`--root`, `--work`): no `[`, `]` or `;` in its path, not inside
OneDrive, and a PC clock not behind the port's files. A required file that is missing, wrong or different stops the check, and the build with it;
an optional one (the trophy set) only gets its line. The tested versions:

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `PARAM.SFO` | 1,040 | `352712c456e94be80fca0cd46dda271b96e81d7bf39365cf13448415dca6f642` |
| `TROPDIR/NPWR03819_00/TROPHY.TRP` | | `6a770bcec111b64c66a5f674f2107b8957334845d4876f4e8dbcfc93bbc10858` |
| `EBOOT.elf` | 19,706,080 | `c81aa91e35a76ff7d856095c392cee98bcc4b01879d7f13e5a389c6ee2d903fc` |
| `data_ps3.psarc` (also what the `.edat` opens to) | 3,147,174,243 | `341f3c61ee14836047659ebca863ab454fff14f712301ccf8e7f9029deb65d2d` |
| `USRDIR/data_ps3.psarc.edat` | 3,153,321,376 | `3c85c5321bccab33079896f5e74c06902e84648154b315af21e14878d93fe58b` |
| `USRDIR/data_ps3.psarc.edat` (PlayStation Store copy; your RPCS3 opens it) | 3,153,321,376 | `c130493bc0d9b32bf00aa42e4e24cce1187c76f802838929267ad3255241962a` |

The European edition (NPEA00418), measured on 2026-11-24 from the PlayStation Store's package:

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `PARAM.SFO` | 1,040 | `039e155c9de824d8b8f73eb8c4cac7d84ebac395be8e7d51e47dd9a338ac02ea` |
| `TROPDIR/NPWR03819_00/TROPHY.TRP` | | `6a770bcec111b64c66a5f674f2107b8957334845d4876f4e8dbcfc93bbc10858` (the same set) |
| `EBOOT.elf` | 19,706,080 | `e4fc4f13b1bd940f52c5df16f4cc4bf0af88e17f4c1d2276b877a62490c57f52` |
| `data_ps3.psarc` (what your RPCS3 opens the `.edat` to) | 3,416,897,765 | `a6ca5f9ad6d2d27d90d9c1d539f3f3d95e4b6ed70639d44bbcde4247ea65b79d` |
| `USRDIR/data_ps3.psarc.edat` | 3,423,571,680 | `1001a9f1ff55c23df1f626d2c1f2b0506638faa3b5ddb8400b05dbef6d203ae0` |

What you give from a copy of the European disc (BCES-01743, version 01.00), measured on 2026-11-25:

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `PS3_GAME/PARAM.SFO` | 1,040 | checked by its fields (title `BCES01743`, version 01.00); `3e728a1f14749043c007ee7b729bc70b8108b1b6f2bcbe2fe1c7a8125b618e5e` on the disc |
| `PS3_GAME/USRDIR/EBOOT.BIN` | 19,708,512 | `63cf04eddf64b8fba0b41ca7c63e329d135f08b99e97fa93308abd99b9e938ed` (decrypts to the `EBOOT.elf` above) |
| `PS3_GAME/USRDIR/data_ps3.psarc` | 3,416,897,765 | `a6ca5f9ad6d2d27d90d9c1d539f3f3d95e4b6ed70639d44bbcde4247ea65b79d` (the `data_ps3.psarc` above) |
| `PS3_GAME/TROPDIR/NPWR03819_00/TROPHY.TRP` | 4,599,680 | `6a770bcec111b64c66a5f674f2107b8957334845d4876f4e8dbcfc93bbc10858` (the same set) |

The lift has addresses of these EBOOTs built in (`tools/lift.sh`, `re/function_seeds.txt`, `re/eu/address_map.tsv`,
the native functions in `port/kz1hd/`), so a different EBOOT will not work.

## Then

[docs/BUILDING.md](BUILDING.md): patch ps3recomp, lift, set up the run folder, build and play.
