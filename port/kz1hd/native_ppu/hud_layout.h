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
 * In split screen (two local players) each view's HUD is laid out in its
 * own 1280x360 band: the frame's ortho is (0, 1280, 360, 0), the quads come
 * with y scale 1 and offset 0, a panel's bottom at y 349 (seen 2026-11-25).
 * hud_band_h is that layout's height; the HUD's sides and halves are taken
 * in it.
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

/* the height of one view's HUD layout: 720, 360 per view in split screen */
static inline double hud_band_h(int players) { return players == 2 ? 360.0 : 720.0; }

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
#endif
