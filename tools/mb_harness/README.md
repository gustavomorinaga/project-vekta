# Motion blur harness (docs/dev/wws-jm-post.md, "The motion blur outside the game")

Runs the lifted SPU code of any captured `jm_post` job outside the game, after `tools/mlaa_harness`.
Nothing here is game data; captures and the build stay on the SSD (`MB_WORK`, default `.../work/recomp/kz1hd/mb_cap`).

- Capture: `KZ_JM_CAPTURE=<dir> KZ_JM_CAPTURE_MODULES=200,201,...` (`port/kz1hd/native/jm.c`) with `KZ_MLAA=spu KZ_JM_HLE=0`;
  `NV0039_STATS_GO=<dir>/go` (cellGcmSys.c) starts it when the motion field moves (sprinting).
- `build.sh [out dir]`: the harness, linked with the lifted `85fc`, `af58`, `14e0`, `de0b`, `f601` and the policy module.
- `harness <cap> info | recs <job> | replay <job> | run <j0> <j1> [ea=file]... [!ea+len=file]... [@ea+len=file]...`: see `harness.c`.
  `de0b`, `14e0` and `f601` take their input from the descriptor's DMA list, which the policy module prefetches while the
  previous job runs; the harness gives those transfers to the job they feed and redoes them from main memory at its start.
- `jobs.py`: a capture's transfers per job, from Python (no prefetch reassignment).
- `check_mb.py <cap> [seed]`: each stage of one frame (Modulate, the field blurs, Downsample, MotionBlur,
  Upsample's blend) through the harness and through the C reference (`port/kz1hd/native/mb_ref.c`) on random inputs.
- `mb_ref_cli field|down|gather|blend|all ...`: the C reference on raw A,R,G,B files (see the top of the file).
- `mb_vk_test shader.spv in.argb field.argb out.argb [repeats]`: the GPU pass (ps3recomp
  `libs/video/vulkan/rsx_vk_mb.comp`) headless, same stages as the renderer; prints the GPU time (`MB_STAGES=1`: per stage).
- `regress_mb.sh [cap]`: builds all of the above and checks lifted code = C (`check_mb.py`) and GPU = C.
- In the game: `PS3RECOMP_MLAA_DUMP=<dir> PS3RECOMP_MLAA_DUMP_MOVING=1` and `touch <dir>/go` while sprinting dumps
  8 frames (`_in` before the MLAA, `_field`, `_out`); `mlaa_ref_cli` then `mb_ref_cli all` on `_in`/`_field` must give `_out`.
