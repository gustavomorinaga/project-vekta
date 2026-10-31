#!/bin/sh
# Makes the folder the port runs from ($KZ_VFS, default work/vfs) out of your own files (tools/env.sh). The game asks
# for /dev_bdvd/PS3_GAME/..., which the runtime resolves under the current folder:
#   PS3_GAME/PARAM.SFO                  copied from the installed game
#   PS3_GAME/TROPDIR/                   copied from the installed game (sceNpTrophy reads TROPHY.TRP's names)
#   PS3_GAME/USRDIR/EBOOT.elf           a link to the decrypted EBOOT
#   PS3_GAME/USRDIR/data_ps3.psarc      a link to the decrypted archive
#   PS3_GAME/USRDIR/{Movies,Menu}/*.avi the movies, taken out of the archive (sail.cpp plays them from there)
# Saves, kz1hd_pc.ini and kz1hd_keys.ini are written next to them on the first run; running this again keeps them.
# The steps themselves are in tools/kzbuild.py (the same code on Windows); this runs them.
exec python3 "$(dirname "$(readlink -f "$0")")/kzbuild.py" vfs "$@"
