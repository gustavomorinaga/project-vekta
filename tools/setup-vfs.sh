#!/bin/sh
# Makes the folder the port runs from ($KZ_VFS, default work/vfs) out of your own files (tools/env.sh). The game asks
# for /dev_bdvd/PS3_GAME/..., which the runtime resolves under the current folder:
#   PS3_GAME/PARAM.SFO                  copied from the installed game
#   PS3_GAME/TROPDIR/                   copied from the installed game (sceNpTrophy reads TROPHY.TRP's names)
#   PS3_GAME/USRDIR/EBOOT.elf           a link to the decrypted EBOOT
#   PS3_GAME/USRDIR/data_ps3.psarc      a link to the decrypted archive
#   PS3_GAME/USRDIR/{Movies,Menu}/*.avi the movies, taken out of the archive (sail.cpp plays them from there)
# Saves, kz1hd_pc.ini and kz1hd_keys.ini are written next to them on the first run; running this again keeps them.
set -e
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
. "$KZ_ROOT/tools/env.sh"
for f in "$KZ_GAME/PARAM.SFO" "$KZ_EBOOT" "$KZ_PSARC"; do
    [ -f "$f" ] || { echo "no $f (KZ_GAME, KZ_EBOOT, KZ_PSARC; docs/GETTING-FILES.md)" >&2; exit 1; }
done
U="$KZ_VFS/PS3_GAME/USRDIR"
mkdir -p "$U"
cp "$KZ_GAME/PARAM.SFO" "$KZ_VFS/PS3_GAME/"
if [ -d "$KZ_GAME/TROPDIR" ]; then
    cp -r "$KZ_GAME/TROPDIR" "$KZ_VFS/PS3_GAME/"
else
    echo "no TROPDIR in $KZ_GAME: trophies are logged without their names"
fi
ln -sfn "$(readlink -f "$KZ_EBOOT")" "$U/EBOOT.elf"
ln -sfn "$(readlink -f "$KZ_PSARC")" "$U/data_ps3.psarc"
python3 "$KZ_ROOT/tools/psarc_ls.py" "$KZ_PSARC" "$U" .avi > /dev/null
echo "vfs: $KZ_VFS ($(find "$U" -name '*.avi' | wc -l) movies)"
