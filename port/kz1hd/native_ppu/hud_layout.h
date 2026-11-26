/*
 * How far the 2D layer and the HUD's sides move for the cameras' aspect
 * (hud.cpp). Header-only, plain C, so a host test can check it
 * (tools/tests/test_hud_layout.c).
 *
 * The frame is rendered anamorphic to the window's aspect and stretched to
 * it; the game's 2D layout is 1280x720. hud_widen is the 2D layer's width
 * over 16:9's: above 1 for a wider window (21:9, 32:9), below 1 for a
 * narrower one (4:3, 16:10), where only the middle 1280 x hud_widen of the
 * layout is on screen, at its own shape. hud_spread is where the HUD's
 * sides go, from the layout's middle: wider than 16:9 between the 16:9
 * screen's edges (HUD position 0) and the window's (1); narrower, always at
 * the window's edges, as the original keeps its HUD in the corners of a 4:3
 * screen (its layout is not changed by the game's 4:3 mode: the HUD module
 * never reads the setting, EBOOT 2026-11-12).
 *
 * In split screen (two local players) each view is drawn in its own
 * 1280x360 band: the frame's ortho is (0, 1280, 360, 0), the quads come with
 * y scale 1 and offset 0 (seen 2026-11-25); hud_band_h is the band's height.
 * ENHANCEMENT: there the HUD is made to lay itself out in the 1280x720
 * layout of one player (hud.cpp hud_viewport), and the whole layout is
 * scaled by hud_band_k into the band, quads and text alike, around the sides
 * they go with: each player's panels keep one player's shape at half its
 * size, and the text stays where it is on its panel. The game's own split
 * HUD is laid out in the band's 1280x360: its heights halve but its panels'
 * end pieces keep their widths (health panel 257x23 against 354x46 with one
 * player), and its ammo count stands 2.7 units higher on its bar (RPCS3 and
 * the port at 16:9, 2026-11-25).
 */
#ifndef KZ_HUD_LAYOUT_H
#define KZ_HUD_LAYOUT_H

#define HUD_ASPECT_169 (16.0f / 9.0f)

/* the 2D layer's width over 16:9's for the cameras' aspect (0: the game's) */
static inline float hud_widen(float aspect)
{
    if (!(aspect > 0.0f)) return 1.0f;
    const float d = aspect - HUD_ASPECT_169;
    return d > 0.01f || d < -0.01f ? aspect / HUD_ASPECT_169 : 1.0f;
}

/* the sides' distance from the middle over the 16:9 layout's */
static inline double hud_spread(double widen, double pos)
{
    return widen < 1.0 ? widen : 1.0 + (widen - 1.0) * pos;
}

/* the height of one view's band on the 720-line frame: 720, 360 per view in split screen */
static inline double hud_band_h(int players) { return players == 2 ? 360.0 : 720.0; }

/* the HUD's 1280x720 layout to one view's band: 1, 0.5 in split screen */
static inline double hud_band_k(int players) { return hud_band_h(players) / 720.0; }

/* the side of an extent v is in: its first third (0), the middle (extent/2)
 * or the last third (extent) */
static inline double hud_third(double v, double extent)
{
    return v < extent / 3 ? 0.0 : v > extent * 2 / 3 ? extent : extent * 0.5;
}

/* The sides a HUD quad or string (middle cx, cy) goes with in a w x h
 * layout: the third of the width it is in; in y the third in the middle
 * third, the half at the sides (a sniper rifle's long column of rounds that
 * reaches the middle third stays with its panel). */
static inline void hud_anchor(double cx, double cy, double w, double h, double* ax, double* ay)
{
    *ax = hud_third(cx, w);
    *ay = *ax == w * 0.5 ? hud_third(cy, h) : cy < h * 0.5 ? 0.0 : h;
}

/* x of a HUD point that goes with side ax, in a layout w wide: the side
 * spreads from the middle (hud_spread), the point keeps its distance from the
 * side scaled by the HUD size */
static inline double hud_place_x(double x, double ax, double w, double spread, double size)
{
    return w * 0.5 + (ax - w * 0.5) * spread + (x - ax) * size;
}

/* y of a HUD point that goes with side ay of the 1280x720 layout, in its
 * band: the side scaled into the band by k (hud_band_k), the point's distance
 * from it by the HUD size and k. With one player (k 1), ay + (y - ay) * size. */
static inline double hud_place_y(double y, double ay, double k, double size)
{
    return ay * k + (y - ay) * size * k;
}

/* The same for a quad's y in its own units, which reach the screen through
 * its y scale and offset (screen y = y * sy + ty; func_00227A1C's r6). With
 * k 1 it is the side in those units plus the distance from it by size, as
 * one player's HUD has always been placed. */
static inline double hud_place_y_quad(double y, double ay, double sy, double ty, double k, double size)
{
    const double a = sy != 0.0 ? (ay - ty) / sy : 0.0, ak = sy != 0.0 ? (ay * k - ty) / sy : 0.0;
    return ak + (y - a) * size * k;
}

/* A HUD string's glyph scales (func_00044108): w, f4, sizes the glyphs'
 * width, h, f5, the font's height, so each axis is drawn on its own. Both
 * go into one view's band by k, so a split-screen string keeps one
 * player's glyph shape at half its size; the HUD size has always reached w
 * only. Before 2026-11-25 only w took k: the band's text came out twice as
 * tall as wide (f4 0.9375 against 1.875, f5 45 in both, KZ_CAM_LOG). */
static inline void hud_glyph(double* w, double* h, double size, double k)
{
    *w = *w * size * k;
    if (k != 1.0) *h = *h * k;
}

/* The rows the game moves the second split-screen view's HUD up
 * (func_004CB798, 0x004CC71C..0x004CC77C: the viewport's y0 and y1 less 20
 * for view 1). The port hands it a viewport that many rows lower, so each
 * player's HUD sits in its own band with the same margins; none with one
 * player. */
static inline int hud_view_lift(int players) { return players == 2 ? 20 : 0; }
#endif
