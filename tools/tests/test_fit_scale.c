/* The frame's render scale and the forced aspect's box (patch 2068, rsx_fit_scale.h). "fill" scales each axis to the
 * window for a frame rendered anamorphic to the window's aspect, now also for a window taller than 16:9 (before, fill
 * only widened, and a 4:3 or 16:10 window got the 16:9 frame with bars); a forced aspect fits its box in the window. */
#include <math.h>
#include <stdio.h>
#include "rsx_fit_scale.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

int main(void)
{
    float sx, sy;
    /* 16:9 window: the same scale on both axes, fill or fit */
    rsx_fit_axes(1920, 1080, 1280, 720, 1.0f, 1, &sx, &sy); EXPECT(sx == 1.5f && sy == 1.5f);
    rsx_fit_axes(1920, 1080, 1280, 720, 1.0f, 0, &sx, &sy); EXPECT(sx == 1.5f && sy == 1.5f);
    /* wider: fill widens */
    rsx_fit_axes(3440, 1440, 1280, 720, 1.0f, 1, &sx, &sy); EXPECT(NEAR(sx, 2.6875f) && sy == 2.0f);
    /* taller (4:3, 16:10): fill now takes the window's height too */
    rsx_fit_axes(1440, 1080, 1280, 720, 1.0f, 1, &sx, &sy); EXPECT(sx == 1.125f && sy == 1.5f);
    rsx_fit_axes(1920, 1200, 1280, 720, 1.0f, 1, &sx, &sy); EXPECT(sx == 1.5f && NEAR(sy, 1200.0f / 720.0f));
    /* fit: the largest 16:9 frame */
    rsx_fit_axes(1440, 1080, 1280, 720, 1.0f, 0, &sx, &sy); EXPECT(sx == 1.125f && sy == 1.125f);
    /* the scale setting multiplies both */
    rsx_fit_axes(1440, 1080, 1280, 720, 0.5f, 1, &sx, &sy); EXPECT(NEAR(sx, 0.5625f) && NEAR(sy, 0.75f));

    /* parsing */
    EXPECT(NEAR(rsx_parse_aspect("4:3"), 4.0f / 3.0f));
    EXPECT(NEAR(rsx_parse_aspect("16:10"), 1.6f));
    EXPECT(NEAR(rsx_parse_aspect("32:9"), 32.0f / 9.0f));
    EXPECT(NEAR(rsx_parse_aspect("1.6"), 1.6f));
    EXPECT(rsx_parse_aspect(NULL) == 0.0f && rsx_parse_aspect("") == 0.0f && rsx_parse_aspect("auto") == 0.0f);
    EXPECT(rsx_parse_aspect("4:0") == 0.0f && rsx_parse_aspect("1:2") == 0.0f && rsx_parse_aspect("9:1") == 0.0f);

    /* the box: pillarbox, letterbox, none */
    int bw, bh;
    rsx_aspect_box(1920, 1080, 4.0f / 3.0f, &bw, &bh); EXPECT(bw == 1440 && bh == 1080);
    rsx_aspect_box(3440, 1440, 16.0f / 9.0f, &bw, &bh); EXPECT(bw == 2560 && bh == 1440);
    rsx_aspect_box(1920, 1080, 32.0f / 9.0f, &bw, &bh); EXPECT(bw == 1920 && bh == 540);
    rsx_aspect_box(1920, 1080, 0.0f, &bw, &bh); EXPECT(bw == 1920 && bh == 1080);
    /* a forced 4:3 in a 16:9 window renders 4:3, anamorphic in the 16:9 frame */
    rsx_aspect_box(1920, 1080, 4.0f / 3.0f, &bw, &bh);
    rsx_fit_axes(bw, bh, 1280, 720, 1.0f, 1, &sx, &sy); EXPECT(sx == 1.125f && sy == 1.5f);
    if (fails) return 1;
    puts("test_fit_scale: ok");
    return 0;
}
