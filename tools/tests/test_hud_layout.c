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
    /* split screen: each view's 1280x360 band holds one player's 1280x720 layout at half its size */
    EXPECT(hud_band_h(2) == 360.0 && hud_band_k(2) == 0.5 && hud_band_k(1) == 1.0 && hud_band_k(0) == 1.0);
    /* one player's health panel (KZ_HUD_SITES, 1P Battlefields, 2026-11-25): 0..354.062 x 651.094..697.5 */
    const double p0x = 0.0, p1x = 354.062, p0y = 651.094, p1y = 697.5;
    hud_anchor((p0x + p1x) * 0.5, (p0y + p1y) * 0.5, 1280.0, 720.0, &ax, &ay);
    EXPECT(ax == 0.0 && ay == 720.0);
    const double k = hud_band_k(2);
    const double q0x = hud_place_x(p0x, ax, 1280.0, 1.0, 1.0 * k), q1x = hud_place_x(p1x, ax, 1280.0, 1.0, 1.0 * k);
    const double q0y = hud_place_y_quad(p0y, ay, 1.0, 0.0, k, 1.0), q1y = hud_place_y_quad(p1y, ay, 1.0, 0.0, k, 1.0);
    EXPECT(NEAR(q0x, 0.0) && NEAR(q1x, 177.031) && NEAR(q0y, 325.547) && NEAR(q1y, 348.75));   /* in the band, at its bottom */
    /* the same shape as one player's (the game's own split panel, 257.031 x 23.203, is not) */
    EXPECT(NEAR((q1x - q0x) / (q1y - q0y), (p1x - p0x) / (p1y - p0y)));
    /* the ammo count on its bar: one player's bar 651.094..697.5, its text at y 665.324, glyphs 1.54688
     * (x 1114.01, the right side); in the band text and quad move and scale together, so the text sits on the bar
     * exactly as with one player, at half the size */
    const double ty1 = 665.324, tx1 = 1114.01, f4 = 1.54688;
    double tax, tay;
    hud_anchor(tx1, ty1, 1280.0, 720.0, &tax, &tay);
    EXPECT(tax == 1280.0 && tay == 720.0);
    const double ty2 = hud_place_y(ty1, tay, k, 1.0), f42 = f4 * 1.0 * k;
    const double b0 = hud_place_y_quad(p0y, 720.0, 1.0, 0.0, k, 1.0), b1 = hud_place_y_quad(p1y, 720.0, 1.0, 0.0, k, 1.0);
    EXPECT(NEAR((ty2 - b0) / (b1 - b0), (ty1 - p0y) / (p1y - p0y)));
    EXPECT(NEAR(f42 / (b1 - b0), f4 / (p1y - p0y)));
    EXPECT(NEAR(hud_place_x(tx1, tax, 1280.0, 1.0, k), 1280.0 - (1280.0 - tx1) * 0.5));
    /* HUD size multiplies the band's scale, around the same sides */
    EXPECT(NEAR(hud_place_y(p0y, 720.0, k, 1.5), 360.0 - (720.0 - p0y) * 0.75));
    /* the crosshair (120x90 in the middle of one player's layout) in the band's middle, 60x45 as the game's own */
    EXPECT(NEAR(hud_place_y(315.0, 360.0, k, 1.0), 157.5) && NEAR(hud_place_y(405.0, 360.0, k, 1.0), 202.5));
    /* the glyphs' shape (func_00044108: f4 the width's scale, f5 the font's height; KZ_CAM_LOG, 2026-11-25): the timer
     * 1.875 / 45 and the ammo count 1.54688 / 37.125 with one player. In the band both halve, so a string keeps
     * one player's shape at half its size (before, f5 stayed 45: the text twice as tall as wide) */
    {
        const double f4s[] = { 1.875, 1.54688 }, f5s[] = { 45.0, 37.125 };
        for (int i = 0; i < 2; i++) {
            double w = f4s[i], h = f5s[i];
            hud_glyph(&w, &h, 1.0, k);
            EXPECT(NEAR(w, f4s[i] * 0.5) && NEAR(h, f5s[i] * 0.5));
            EXPECT(NEAR(w / h, f4s[i] / f5s[i]));
            /* one player, any HUD size: f4 as before (f4 * size), f5 untouched */
            const double sizes[] = { 1.0, 0.75, 1.5 };
            for (int j = 0; j < 3; j++) {
                w = f4s[i]; h = f5s[i];
                hud_glyph(&w, &h, sizes[j], hud_band_k(1));
                EXPECT(w == f4s[i] * sizes[j] * 1.0 && h == f5s[i]);
            }
        }
    }
    /* the second view's HUD: the game sets its viewport 20 rows up (0, 340, 1280x360, VPLOG 2026-11-25); from the
     * lifted rectangle it lands on the view's own band, 360..720, as the first view's on 0..360 */
    EXPECT(hud_view_lift(1) == 0 && hud_view_lift(0) == 0);
    EXPECT((360 + hud_view_lift(2)) - 20 == 360 && (720 + hud_view_lift(2)) - 20 == 720);
    /* one player: exactly the earlier arithmetic, bit for bit */
    {
        const double ys[] = { 651.094, 697.5, 25.0842, 509.344, 0.0, 720.0 }, sizes[] = { 1.0, 0.75, 1.5, 1.3 };
        const double ays[] = { 0.0, 360.0, 720.0 }, sys[] = { 1.0, 2.0, 0.5 }, tys[] = { 0.0, 3.25, -7.5 };
        for (int i = 0; i < 6; i++)
            for (int j = 0; j < 4; j++)
                for (int a = 0; a < 3; a++) {
                    const double y = ys[i], sz = sizes[j], ay1 = ays[a];
                    EXPECT(hud_place_y(y, ay1, hud_band_k(1), sz) == ay1 + (y - ay1) * sz);
                    for (int b = 0; b < 3; b++) {
                        const double sy = sys[b], ty = tys[b], ayq = (ay1 - ty) / sy;
                        EXPECT(hud_place_y_quad(y, ay1, sy, ty, hud_band_k(1), sz) == ayq + (y - ayq) * sz);
                    }
                    EXPECT(hud_place_x(y, ay1, 1280.0, 1.25, sz * hud_band_k(1)) == hud_place_x(y, ay1, 1280.0, 1.25, sz));
                }
    }
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
