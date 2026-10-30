#!/bin/bash
S=${S:-/tmp};  T=${KZ_TESTKIT:?set KZ_TESTKIT to a test kit with b7.sh and a Level 7 save (not published)}
D=${OUT:-$S/nvd}; rm -rf $D; mkdir -p $D; rm -f $S/b7_nvd.log
export NV0039_PATTERN=${PAT:-} KZ_JM_SKIP=0 PS3RECOMP_ENG_MAIN_COHERENCE=1 PS3RECOMP_NV0039=1 NV0039_DUMP=$D B7LOG=$S/b7_nvd.log
( B7PLAY=40 $T/b7.sh ${KZ_BIN:-$(cd "$(dirname "$0")/../.." && pwd)/work/build/KZ1HD} > /dev/null 2>&1 ) &
for i in $(seq 400); do grep -a "kz-stats" $S/b7_nvd.log 2>/dev/null | tail -1 | sed "s/.*ticks=//" | awk "{v=\$1} END{exit !(v+0>10000)}" && break; sleep 1; done
sleep 15; touch $D/go; wait; ls $D
