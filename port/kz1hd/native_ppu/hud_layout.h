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
#endif
