#!/bin/bash
# capture.sh: Level 7 (testkit save, 144 mode) with the SPU post chain on and KZ_JM_HLE=0 (lanes only);
# 15 s into play, KZ_JM_CAPTURE writes 3 frames of the chain's transfers and the MLAA jobs' entry
# states into $CAP, and NV0039_DUMP the frames sent to / returned by the chain into $OUT.
# PAT=<P6 1280x720>: the sent frames are replaced by that image (NV0039_PATTERN). S: work dir for the log.
S=${S:-/tmp}; T=${KZ_TESTKIT:?set KZ_TESTKIT to a test kit with b7.sh and a Level 7 save (not published)}
D=${OUT:?}; C=${CAP:?}; rm -rf $D $C; mkdir -p $D $C; rm -f $S/b7_cap.log
export NV0039_PATTERN=${PAT:-} KZ_JM_SKIP=0 KZ_JM_HLE=0 PS3RECOMP_ENG_MAIN_COHERENCE=1 PS3RECOMP_NV0039=1 NV0039_DUMP=$D KZ_JM_CAPTURE=$C B7LOG=$S/b7_cap.log
( B7PLAY=40 $T/b7.sh ${KZ_BIN:-$(cd "$(dirname "$0")/../.." && pwd)/work/build/KZ1HD} > /dev/null 2>&1 ) &
for _ in $(seq 400); do grep -a "kz-stats" $S/b7_cap.log 2>/dev/null | tail -1 | sed "s/.*ticks=//" | awk "{v=\$1} END{exit !(v+0>10000)}" && break; sleep 1; done
sleep 15; touch $C/go $D/go; wait; ls $D | wc -l; ls $C | wc -l
