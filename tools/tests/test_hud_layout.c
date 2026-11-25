/* The 2D layer and the HUD's sides for the cameras' aspect (port/kz1hd/native_ppu/hud_layout.h). Below 16:9 the
 * layer narrows and the HUD's sides go to the window's edges whatever the HUD position (before, a window narrower
 * than 16:9 kept the 16:9 picture with bars, and the sides at 16:9's would be off a 4:3 screen). */
#include <math.h>
#include <stdio.h>
#include "hud_layout.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-4)

int main(void)
{
    EXPECT(hud_widen(0.0f) == 1.0f);                        /* the game's 16:9 */
    EXPECT(hud_widen(16.0f / 9.0f) == 1.0f);
    EXPECT(NEAR(hud_widen(3440.0f / 1440.0f), (3440.0 / 1440.0) / (16.0 / 9.0)));
    EXPECT(NEAR(hud_widen(4.0f / 3.0f), 0.75));             /* 960 of the 1280 layout on screen */
    EXPECT(NEAR(hud_widen(1.6f), 0.9));
    /* wider: from the 16:9 edges (0) to the window's (1) */
    EXPECT(hud_spread(1.5, 0.0) == 1.0 && hud_spread(1.5, 1.0) == 1.5 && hud_spread(1.5, 0.5) == 1.25);
    /* narrower: at the window's edges, whatever the position */
    EXPECT(hud_spread(0.75, 0.0) == 0.75 && hud_spread(0.75, 1.0) == 0.75);
    /* a 4:3 window: the layout's left edge (x 0) lands on the window's left edge, x 160 of the narrowed layer */
    EXPECT(NEAR(640.0 + (0.0 - 640.0) * hud_spread(hud_widen(4.0f / 3.0f), 1.0), 640.0 - 640.0 * 0.75));
    EXPECT(hud_spread(1.0, 0.3) == 1.0);

    /* one player: the 1280x720 layout as before (a panel at the bottom left, the timer at the top middle) */
    double ax, ay;
    EXPECT(hud_band_h(1) == 720.0 && hud_band_h(0) == 720.0);
    hud_anchor(128.0, 685.0, 1280.0, hud_band_h(1), &ax, &ay);
    EXPECT(ax == 0.0 && ay == 720.0);
    hud_anchor(1230.0, 337.0, 1280.0, hud_band_h(1), &ax, &ay);   /* a right-side column above the middle */
    EXPECT(ax == 1280.0 && ay == 0.0);
    hud_anchor(622.0, 30.0, 1280.0, hud_band_h(1), &ax, &ay);
    EXPECT(ax == 640.0 && ay == 0.0);
    /* split screen: each view's 1280x360 band; the health panel (y 325-349, seen 2026-11-25) goes with the
     * band's bottom, not the top of a 720 layout */
    EXPECT(hud_band_h(2) == 360.0);
    hud_anchor(128.0, 337.0, 1280.0, hud_band_h(2), &ax, &ay);
    EXPECT(ax == 0.0 && ay == 360.0);
    hud_anchor(1230.0, 337.0, 1280.0, hud_band_h(2), &ax, &ay);
    EXPECT(ax == 1280.0 && ay == 360.0);
    hud_anchor(622.0, 30.0, 1280.0, hud_band_h(2), &ax, &ay);   /* the timer: top middle */
    EXPECT(ax == 640.0 && ay == 0.0);
    hud_anchor(640.0, 180.0, 1280.0, hud_band_h(2), &ax, &ay);   /* the band's middle */
    EXPECT(ax == 640.0 && ay == 180.0);
    /* x: identity at 16:9 and size 1; at 43:18 with position 1 the panel's left edge lands on the widened layer's */
    EXPECT(hud_place_x(100.0, 0.0, 1280.0, 1.0, 1.0) == 100.0);
    const double e = hud_widen(3440.0f / 1440.0f);
    EXPECT(NEAR(hud_place_x(0.0, 0.0, 1280.0, hud_spread(e, 1.0), 1.0), 640.0 - 640.0 * e));
    EXPECT(NEAR(hud_place_x(1280.0, 1280.0, 1280.0, hud_spread(e, 1.0), 1.0), 640.0 + 640.0 * e));
    EXPECT(NEAR(hud_place_x(257.0, 0.0, 1280.0, 1.0, 1.5), 385.5));   /* size 1.5 around the left side */
    if (fails) return 1;
    puts("test_hud_layout: ok");
    return 0;
}
