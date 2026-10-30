# MLAA probe (docs/dev/wws-jm-post.md, "O MLAA medido no próprio módulo")

- `OUT=dir python3 pattern.py` / `pattern2.py`: write a 1280x720 P6 test image (and a layout JSON) into `dir`.
- `PAT=<ppm> OUT=<dump dir> S=<work dir> ./run_l7.sh`: Level 7 from the testkit save, with the SPU post chain on
  (`KZ_JM_SKIP=0`, coherence, `PS3RECOMP_NV0039=1`); 15 s into play the frame going to the SPUs is replaced by
  the pattern (`NV0039_PATTERN`) and the next 8 full-frame NV0039 copies are dumped (`NV0039_DUMP`).
- `pair.py`: `pair(dir)` returns (frame sent, same buffer coming back processed).
