# shellcheck shell=bash
# Shared paths for the build and run scripts (sourced, not run). Every value can be set in the environment
# first; the defaults keep everything inside the repository, in folders git ignores:
#
#   Game/   your own files (docs/GETTING-FILES.md):
#             Game/NPUA80856/        the game as installed from the Killzone Trilogy disc's PS3_EXTRA package
#                                    (PARAM.SFO, TROPDIR/, USRDIR/), e.g. copied from RPCS3's dev_hdd0/game/
#             Game/EBOOT.elf         the decrypted EBOOT
#             Game/data_ps3.psarc    the decrypted data archive
#             Game/libsre.prx        the decrypted libsre from your PS3 firmware (4.93 tested)
#   work/   everything the scripts make from them: the patched ps3recomp, the lift, the run folder (vfs/)
#
# PS3_DEV_FLASH is RPCS3's dev_flash (the firmware's MP3 decoder, flashMP3.pic, comes from there).

# The caller sets KZ_ROOT (the repository root) before sourcing: POSIX sh cannot find a sourced file's own path.
: "${KZ_ROOT:?set KZ_ROOT to the repository root before sourcing tools/env.sh}"
KZ_GAME=${KZ_GAME:-$KZ_ROOT/Game/NPUA80856}
KZ_EBOOT=${KZ_EBOOT:-$KZ_ROOT/Game/EBOOT.elf}
KZ_PSARC=${KZ_PSARC:-$KZ_ROOT/Game/data_ps3.psarc}
KZ_LIBSRE=${KZ_LIBSRE:-$KZ_ROOT/Game/libsre.prx}
KZ_WORK=${KZ_WORK:-$KZ_ROOT/work}
KZ_VFS=${KZ_VFS:-$KZ_WORK/vfs}
PS3RECOMP_DIR=${PS3RECOMP_DIR:-$KZ_WORK/ps3recomp-patched}
PS3_DEV_FLASH=${PS3_DEV_FLASH:-$HOME/.config/rpcs3/dev_flash}
KZ_PY=${KZ_PY:-$KZ_WORK/venv/bin/python}
# cmake and ninja from the venv when they are there (docs/BUILDING.md)
if [ -d "$KZ_WORK/venv/bin" ]; then PATH="$KZ_WORK/venv/bin:$PATH"; fi
export PATH KZ_ROOT KZ_GAME KZ_EBOOT KZ_PSARC KZ_LIBSRE KZ_WORK KZ_VFS PS3RECOMP_DIR PS3_DEV_FLASH KZ_PY
