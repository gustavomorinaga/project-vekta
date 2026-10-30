# MLAA harness (docs/dev/wws-jm-post.md, "The MLAA outside the game")

Runs the lifted SPU code of `Gfx.Post.Transpose` (`85fc`) and `Gfx.Post.MLAA` (`af58`) outside the game,
on any 1280x720 image, from a capture of the jobs' entry states. Nothing here is game data; the captures,
the lifted code and the build stay on the SSD (`MLAA_WORK`, default `.../work/recomp/kz1hd/mlaa_cap`).

- `capture.sh` (`CAP=<dir> OUT=<dir> [PAT=<ppm>]`): the in-game capture (`KZ_JM_CAPTURE`, `port/kz1hd/native/jm.c`).
- `build.sh <out dir>`: the harness, linked with the lifted `85fc`, `af58` and policy module (`SPU_DIR`, `PS3RECOMP_DIR`).
- `harness <cap> info | replay <job> | chain <job> <in> <out>`: see the top of `harness.c`.
  `HARNESS_FIRST`/`HARNESS_NJOBS` run part of the chain (0/9 = column pass, 9/5 = row pass),
  `HARNESS_RAW` dumps the whole 1280x768 buffer, `HARNESS_TRACE`/`HARNESS_PROF` the transfers.
- `mlaa.py`: `run(img, first, n, cap)` from Python. `capframes.py`: the game's buffer before/after each chain of a capture.
- `check_harness.py <cap>...`: every job replayed and every chain rerun against the game, byte for byte.
- `mlaa_ref_cli in.argb out.argb`: the C reference (`port/kz1hd/native/mlaa_ref.c`) on a raw 1280x720 A,R,G,B image.
- `mlaa_vk_test shader.spv in.argb out.argb [repeats]`: the GPU pass (ps3recomp `libs/video/vulkan/rsx_vk_mlaa.comp`)
  headless, same stages as the renderer; prints the GPU time (`MLAA_STAGES=1`: per stage).
- `regress_mlaa.sh`: builds all of the above and runs `check_mlaa.py` (lifted code = game, C = lifted code, GPU = C).
