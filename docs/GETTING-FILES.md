# Getting your files ready

The port is built from your own copy of the game. This page says which files the scripts need, where they look for
them, and how they are checked. It does not explain how to decrypt anything: this project ships no decryption tools
or keys and gives no such instructions. The files below come from your own disc and your own PS3 firmware.

## What you need

| File | What it is | Where the scripts look (`tools/env.sh`) | Required |
|---|---|---|---|
| The installed game folder | Killzone HD, title **NPUA80856**, as installed from the `PS3_EXTRA` package of the **Killzone Trilogy** disc (US, BCUS98116): `PARAM.SFO`, `TROPDIR/`, `USRDIR/` | `Game/NPUA80856/` (`KZ_GAME`) | yes |
| `EBOOT.elf` | the game's executable, decrypted: an ELF file, not the `EBOOT.BIN` container in `USRDIR` | `Game/EBOOT.elf` (`KZ_EBOOT`) | yes |
| `data_ps3.psarc` | the game's data archive, decrypted: a PSARC file, not the `data_ps3.psarc.edat` the package installs | `Game/data_ps3.psarc` (`KZ_PSARC`) | yes |
| `libsre.prx` | the SPURS library from PS3 system software 4.93, decrypted (in the firmware it is `sys/external/libsre.sprx`) | `Game/libsre.prx` (`KZ_LIBSRE`) | yes |
| `dev_flash` | an installed PS3 system software 4.93, as RPCS3 keeps it; the port reads `sys/external/flashMP3.pic` from it as it is | `~/.config/rpcs3/dev_flash` (`PS3_DEV_FLASH`) | for music and sound |

Why the firmware pieces: the game sends work to an SPU task through a SPURS queue whose PPU side is code in
`libsre`, and its music decoder is loaded from `flashMP3.pic`. The port runs that code, translated like the game's,
instead of rewriting it.

You can keep the files anywhere: set the variables in the third column to point at them, or link them into `Game/`.
`Game/` and `work/` are ignored by git, so nothing you put there can be committed by accident.

## Checking them

```sh
tools/check-game-files.py          # size, type and SHA-256 of each file
tools/check-game-files.py --quick  # skips hashing the 3 GB archive
```

Each line says `ok`, `MISSING`, `WRONG` (not the right kind of file: for example the `.edat` instead of the
decrypted archive, or `EBOOT.BIN` instead of `EBOOT.elf`) or `DIFFERENT` (the right kind of file, but not the version
the port was made from). The tested versions:

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `PARAM.SFO` | 1,040 | `352712c456e94be80fca0cd46dda271b96e81d7bf39365cf13448415dca6f642` |
| `TROPDIR/NPWR03819_00/TROPHY.TRP` | | `6a770bcec111b64c66a5f674f2107b8957334845d4876f4e8dbcfc93bbc10858` |
| `EBOOT.elf` | 19,706,080 | `c81aa91e35a76ff7d856095c392cee98bcc4b01879d7f13e5a389c6ee2d903fc` |
| `data_ps3.psarc` | 3,147,174,243 | `341f3c61ee14836047659ebca863ab454fff14f712301ccf8e7f9029deb65d2d` |
| `libsre.prx` (4.93) | 239,344 | `9e7ee6edc99b1d372686cf927b883b18f19ed4d32d1faf35e214cca4ae0737b6` |
| `flashMP3.pic` (4.93) | 69,695 | `adbd993483a41f53c994e080665fb5fbf36af783956d0e4ded30057b8b4e894a` |

The lift has addresses of this EBOOT built in (`tools/lift.sh`, `re/function_seeds.txt`, the native functions in
`port/kz1hd/`), so a different EBOOT will not work. The firmware pieces from another system software version may
work but have not been tried (`ASSUMPTION:` their code is unchanged across late versions).

## Then

[docs/BUILDING.md](BUILDING.md): patch ps3recomp, lift, set up the run folder, build and play.
