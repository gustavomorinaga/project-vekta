/* The crosshair's size (port/kz1hd/native_ppu/crosshair.h): size 1 leaves the game's rectangle untouched; the slider's
 * middle is the game's size and its ends the limits; any other size keeps each piece's middle where the game put it
 * (the spread's opening), puts its edges on whole output pixels, symmetric about the screen's middle and at least one
 * pixel wide, and gives the same size in pixels on 16:9 and 32:9 screens of the same height. */
#include <math.h>
#include <stdio.h>
#include "crosshair.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL (line %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

/* pixel edges of a rectangle on a w x h output showing layer_w x layer_h units, centred */
static void to_px(const double r[4], double w, double h, double lw, double lh, double p[4])
{
    const double k = fmin(w / lw, h / lh);
    p[0] = w * 0.5 + (r[0] - lw * 0.5) * k; p[2] = w * 0.5 + (r[2] - lw * 0.5) * k;
    p[1] = h * 0.5 + (r[1] - lh * 0.5) * k; p[3] = h * 0.5 + (r[3] - lh * 0.5) * k;
}
static int whole(double v) { return fabs(v - nearbyint(v)) < 1e-6; }

int main(void)
{
    /* the sites: the crosshair's, and none of the HUD's others (the grenade row, the ammo panel) */
    CHECK(!crosshair_site(0x004CDC0C) && !crosshair_site(0x004CDCC0), "the grenade row is not the crosshair");
    CHECK(!crosshair_site(0x004CE1D8), "the ammo panel is not the crosshair");
    CHECK(crosshair_site(0x004CBBB4), "0x004CBBB4 (func_004CB798) draws the crosshair");
    CHECK(!crosshair_site(0x004CBBB0) && !crosshair_site(0x004CBBB8), "neighbouring addresses are not sites");

    /* size 1: bit for bit the game's */
    {
        double r[4] = { 637.3, 358.9, 642.7, 361.1 }, o[4] = { 637.3, 358.9, 642.7, 361.1 };
        crosshair_rect(r, 1.0, 2560, 1440, 1280, 720);
        for (int i = 0; i < 4; i++) CHECK(r[i] == o[i], "size 1 changed edge %d", i);
    }
    /* the slider */
    CHECK(crosshair_from_slider(0.5) == 1.0, "slider middle is %g", crosshair_from_slider(0.5));
    CHECK(crosshair_from_slider(0.0) == CROSSHAIR_MIN && crosshair_from_slider(1.0) == CROSSHAIR_MAX, "slider ends");
    CHECK(crosshair_from_slider(-3) == CROSSHAIR_MIN && crosshair_from_slider(7) == CROSSHAIR_MAX, "slider out of range");
    CHECK(crosshair_from_slider(NAN) == 1.0 && crosshair_clamp(NAN) == 1.0, "NaN is the game's size");
    CHECK(crosshair_clamp(0.01) == CROSSHAIR_MIN && crosshair_clamp(99) == CROSSHAIR_MAX, "the ini value is clamped");
    for (double p = 0.0; p <= 1.0; p += 0.05)
        CHECK(fabs(crosshair_to_slider(crosshair_from_slider(p)) - p) < 1e-9, "slider round trip at %g", p);
    for (double p = 0.0; p < 1.0; p += 0.05)
        CHECK(crosshair_from_slider(p + 0.05) > crosshair_from_slider(p), "slider not increasing at %g", p);

    /* a 1-unit dot at 1280x720 shrunk to the minimum: still one pixel */
    {
        double r[4] = { 639.5, 359.5, 640.5, 360.5 }, p[4];
        crosshair_rect(r, CROSSHAIR_MIN, 1280, 720, 1280, 720);
        to_px(r, 1280, 720, 1280, 720, p);
        CHECK(fabs((p[2] - p[0]) - 1.0) < 1e-6 && fabs((p[3] - p[1]) - 1.0) < 1e-6, "dot is %gx%g px", p[2] - p[0], p[3] - p[1]);
        for (int i = 0; i < 4; i++) CHECK(whole(p[i]), "dot edge %d at %g", i, p[i]);
    }
    /* pieces left and right of the middle (the spread's ticks), at fractional sizes and several outputs: whole-pixel
     * edges, mirrored about the middle, the middle kept within half a pixel, never under one pixel */
    {
        const double outs[][3] = { { 1280, 720, 1.0 }, { 1920, 1080, 1.0 }, { 2560, 1440, 1.0 }, { 3440, 1440, 3440.0 / 1440 / (16.0 / 9) },
                                   { 5120, 1440, 2.0 }, { 1366, 768, 1366.0 / 768 / (16.0 / 9) }, { 1281, 721, 1.0 } };
        const double sizes[] = { CROSSHAIR_MIN, 0.63, 0.8, 1.17, 1.33, CROSSHAIR_MAX };
        for (unsigned o = 0; o < sizeof outs / sizeof outs[0]; o++)
            for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
                for (double off = 0.0; off < 40.0; off += 3.7) {
                    const double w = outs[o][0], h = outs[o][1], lw = 1280.0 * (outs[o][2] > 1.0 ? outs[o][2] : 1.0);
                    const double mx = lw * 0.5;
                    double a[4] = { mx + off, 357.6, mx + off + 3.3, 362.4 }, b[4] = { mx - off - 3.3, 357.6, mx - off, 362.4 };
                    double pa[4], pb[4];
                    crosshair_rect(a, sizes[s], w, h, lw, 720);
                    crosshair_rect(b, sizes[s], w, h, lw, 720);
                    to_px(a, w, h, lw, 720, pa); to_px(b, w, h, lw, 720, pb);
                    const int odd = ((int)w & 1) || ((int)h & 1);
                    if (!odd) for (int i = 0; i < 4; i++) CHECK(whole(pa[i]) && whole(pb[i]), "%gx%g size %g: edge %d at %g / %g", w, h, sizes[s], i, pa[i], pb[i]);
                    CHECK(pa[2] - pa[0] >= 1.0 - 1e-9 && pa[3] - pa[1] >= 1.0 - 1e-9, "under one pixel");
                    CHECK(fabs((pa[0] - w * 0.5) + (pb[2] - w * 0.5)) < 1e-6 && fabs((pa[2] - w * 0.5) + (pb[0] - w * 0.5)) < 1e-6,
                          "%gx%g size %g off %g: not mirrored (%g..%g vs %g..%g)", w, h, sizes[s], off, pa[0], pa[2], pb[0], pb[2]);
                    const double k = fmin(w / lw, h / 720.0), want = (mx + off + 1.65 - mx) * k;
                    CHECK(fabs((pa[0] + pa[2]) * 0.5 - w * 0.5 - want) <= 0.5 + 1e-9, "middle moved by more than half a pixel");
                    CHECK(fabs((pa[2] - pa[0]) - 3.3 * k * sizes[s]) <= 0.5 + 1e-9 || 3.3 * k * sizes[s] < 1.0, "width off by more than half a pixel");
                }
    }
    /* the same height in pixels, and the same size, at 16:9 and 32:9 (the layer widens, the units stay) */
    {
        double a[4] = { 636.0, 352.0, 644.0, 368.0 }, b[4] = { 1276.0, 352.0, 1284.0, 368.0 }, pa[4], pb[4];
        crosshair_rect(a, 1.33, 2560, 1440, 1280, 720);
        crosshair_rect(b, 1.33, 5120, 1440, 2560, 720);
        to_px(a, 2560, 1440, 1280, 720, pa); to_px(b, 5120, 1440, 2560, 720, pb);
        CHECK(fabs((pa[3] - pa[1]) - (pb[3] - pb[1])) < 1e-6 && fabs((pa[2] - pa[0]) - (pb[2] - pb[0])) < 1e-6,
              "16:9 %gx%g px, 32:9 %gx%g px", pa[2] - pa[0], pa[3] - pa[1], pb[2] - pb[0], pb[3] - pb[1]);
    }
    /* a flipped rectangle (x1 < x0, a mirrored texture) stays flipped */
    {
        double r[4] = { 650.0, 370.0, 630.0, 350.0 };
        crosshair_rect(r, 0.5, 1280, 720, 1280, 720);
        CHECK(r[0] > r[2] && r[1] > r[3], "flip lost");
        CHECK(fabs(r[0] - 645.0) < 1e-9 && fabs(r[2] - 635.0) < 1e-9, "flipped piece %g..%g", r[2], r[0]);
    }
    if (fails) return 1;
    puts("test_crosshair: ok");
    return 0;
}
