/*
 * Host test for rsx_mlaa_size.h (patch 2080): the sizes Killzone HD's GPU MLAA runs at, its buffers and dispatches.
 *  1. It runs on the title's 1280x720 display buffer at any drawn size the shader holds, and on nothing else.
 *  2. At 1280x720 the jobs and dispatches are the title's and the pass's before the patch: 5 column jobs of 256,
 *     5 row jobs of 144; LOAD 5 x 720 workgroups, REC1 256 or 257 pairs, REC2 723, CHAIN2 144 or 145 rows.
 *  3. At other sizes the 5 jobs cover every line once, the REC dispatches are capped at RSX_MLAA_GROUPS workgroups,
 *     the row dispatches cover the width, and the buffers hold every index the shader writes.
 */
#include "rsx_mlaa_size.h"
#include <stdio.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    CHECK(rsx_mlaa_size_ok(1280, 720, 1280, 720), "1280x720 drawn 1280x720");
    CHECK(rsx_mlaa_size_ok(1280, 720, 2560, 1440), "drawn 2560x1440 (scale 2)");
    CHECK(rsx_mlaa_size_ok(1280, 720, 3440, 1440), "drawn 3440x1440 (fill on an ultrawide)");
    CHECK(rsx_mlaa_size_ok(1280, 720, 1707, 960), "drawn 1707x960 (DLSS Quality's render size at 1440p)");
    CHECK(rsx_mlaa_size_ok(1280, 720, 640, 360), "drawn 640x360 (scale 0.5)");
    CHECK(rsx_mlaa_size_ok(1280, 720, 7680, 4320), "drawn 7680x4320");
    CHECK(!rsx_mlaa_size_ok(640, 360, 640, 360), "a 640x360 target is not the display buffer");
    CHECK(!rsx_mlaa_size_ok(1280, 768, 1280, 768), "the SPUs' 1280x768 buffer is not the display buffer");
    CHECK(!rsx_mlaa_size_ok(1280, 720, 8200, 4600), "wider than the shader's line buffer");
    CHECK(!rsx_mlaa_size_ok(1280, 720, 16, 9), "too small");

    CHECK(rsx_mlaa_span(1280) == 256 && rsx_mlaa_span(720) == 144, "the title's jobs: %u columns, %u rows", rsx_mlaa_span(1280), rsx_mlaa_span(720));
    uint32_t gx, gy;
    rsx_mlaa_dispatch(0, 0, 1280, 720, &gx, &gy); CHECK(gx == 5 && gy == 720, "LOAD at 1280x720: %u x %u", gx, gy);
    rsx_mlaa_dispatch(2, 0, 1280, 720, &gx, &gy); CHECK(gx == 3 && gy == 1, "FIX: %u x %u", gx, gy);
    rsx_mlaa_dispatch(3, 0, 1280, 720, &gx, &gy); CHECK(gx == 256, "REC1 job 0: %u", gx);
    rsx_mlaa_dispatch(3, 1, 1280, 720, &gx, &gy); CHECK(gx == 256, "REC1 job 1: %u (257 pairs, capped at 256 groups)", gx);
    rsx_mlaa_dispatch(4, 1, 1280, 720, &gx, &gy); CHECK(gx == 3 && gy == 257, "CHAIN1 job 1: %u x %u", gx, gy);
    rsx_mlaa_dispatch(4, 4, 1280, 720, &gx, &gy); CHECK(gx == 3 && gy == 257, "CHAIN1 job 4: %u x %u", gx, gy);
    rsx_mlaa_dispatch(6, 0, 1280, 720, &gx, &gy); CHECK(gx == 256, "REC2: %u (723 pairs, capped)", gx);
    rsx_mlaa_dispatch(7, 0, 1280, 720, &gx, &gy); CHECK(gx == 5 && gy == 144, "CHAIN2 job 0: %u x %u", gx, gy);
    rsx_mlaa_dispatch(7, 4, 1280, 720, &gx, &gy); CHECK(gx == 5 && gy == 144, "CHAIN2 job 4: %u x %u", gx, gy);
    rsx_mlaa_dispatch(7, 2, 1280, 720, &gx, &gy); CHECK(gx == 5 && gy == 145, "CHAIN2 job 2: %u x %u", gx, gy);

    static const uint32_t sizes[][2] = { { 1280, 720 }, { 2560, 1440 }, { 3440, 1440 }, { 1707, 960 }, { 1281, 721 },
                                         { 640, 360 }, { 5120, 2880 }, { 7680, 4320 }, { 33, 32 } };
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        const uint32_t w = sizes[k][0], h = sizes[k][1], cj = rsx_mlaa_span(w), rj = rsx_mlaa_span(h);
        /* the 5 column jobs and 5 row jobs cover every line, the last one ends at the last line */
        CHECK(cj * (RSX_MLAA_JOBS - 1) < w && cj * RSX_MLAA_JOBS >= w, "%ux%u: column jobs of %u", w, h, cj);
        CHECK(rj * (RSX_MLAA_JOBS - 1) < h && rj * RSX_MLAA_JOBS >= h, "%ux%u: row jobs of %u", w, h, rj);
        rsx_mlaa_dispatch(1, 0, w, h, &gx, &gy); CHECK(gx * 256 >= w && gy == h, "%ux%u FLAGS: %u x %u", w, h, gx, gy);
        uint32_t pairs = 0;
        for (uint32_t j = 0; j < RSX_MLAA_JOBS; j++) {
            rsx_mlaa_dispatch(3, j, w, h, &gx, &gy); CHECK(gx >= 1 && gx <= RSX_MLAA_GROUPS, "%ux%u REC1 job %u: %u groups", w, h, j, gx);
            rsx_mlaa_dispatch(4, j, w, h, &gx, &gy); CHECK(gx * 256 >= h, "%ux%u CHAIN1 job %u: %u", w, h, j, gx);
            pairs += gy - (j ? 1 : 0);               /* each later job starts one line early (r0 - 1) */
            rsx_mlaa_dispatch(7, j, w, h, &gx, &gy); CHECK(gx * 256 >= w && gy >= 1, "%ux%u CHAIN2 job %u", w, h, j);
        }
        CHECK(pairs == w, "%ux%u: the column jobs cover %u of %u lines", w, h, pairs, w);
        rsx_mlaa_dispatch(6, 0, w, h, &gx, &gy); CHECK(gx >= 1 && gx <= RSX_MLAA_GROUPS, "%ux%u REC2: %u", w, h, gx);
        uint64_t sz[9];
        rsx_mlaa_buffer_sizes(w, h, sz);
        const uint64_t L = w > h ? w : h;
        CHECK(sz[0] == (uint64_t)w * h * 4, "%ux%u texels", w, h);
        CHECK(sz[4] >= (uint64_t)(w + 3) * h * 4, "%ux%u: the column flags with lines 1..3 after the last", w, h);
        /* ops: REC1 writes rows up to w - 1 of length h, REC2 rows up to h + JOBS - 2 of length w, 2 words each */
        CHECK(sz[5] >= (uint64_t)w * h * 8 && sz[5] >= (uint64_t)(h + RSX_MLAA_JOBS - 1) * w * 8, "%ux%u ops", w, h);
        CHECK(sz[7] >= (uint64_t)RSX_MLAA_GROUPS * 5 * L * 4, "%ux%u heads and records", w, h);
        CHECK(sz[8] >= (uint64_t)RSX_MLAA_GROUPS * 9 * (L / 3 + 1) * 4, "%ux%u slots", w, h);
    }
    printf("test_mlaa_size (rsx_mlaa_size.h): %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
