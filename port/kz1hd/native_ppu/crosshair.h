/*
 * The weapon crosshair's size (ENHANCEMENT: the PS3 game has no such option).
 * Header-only, plain C, so a host test can check it (tools/tests/test_crosshair.c).
 *
 * The crosshair is two HUD quads (func_00227A1C, hud.cpp) drawn each frame
 * from one site in func_004CB798, the call returning to 0x004CBBB4: a ring,
 * 120x90 of the 1280x720 layout, then its middle, 60x45, both centred on the
 * aim point, each a whole texture (uv 0..1). Level 1 with KZ_HUD_SITES,
 * KZ_HUD_TRACE and KZ_HUD_SKIP (2026-11-12): skipping the site removes the
 * whole crosshair. Each piece is scaled around its own middle by the
 * "Crosshair Size" setting; where the game put the piece (the spread's
 * opening) is left as it is. The HUD Size setting does not touch them. At size
 * 1 the rectangle is the game's, untouched; at any other size its edges are
 * put on whole pixels of the output, symmetric about the screen's middle and
 * at least one pixel wide, so the crosshair stays sharp and even. The scope
 * overlays, the rocket launcher's lock circle and the damage indicators are
 * not crosshair sites.
 */
#ifndef KZ_CROSSHAIR_H
#define KZ_CROSSHAIR_H
#include <math.h>
#include <stdint.h>

/* The setting's range: the slider's ends. The middle of the slider is the
 * game's size (1). Chosen from captures of Level 1 (2026-11-12) at 1280x720,
 * 2560x1320 and 3440x1320: at 0.5 on 1280x720 the ring is about 38 pixels wide
 * and its lines thin but whole; at 1.5 on 1320 lines the ring is a quarter of
 * the screen's height. */
#define CROSSHAIR_MIN 0.5
#define CROSSHAIR_MAX 1.5

/* Whether a drawing site (the return address of its call to func_00227A1C)
 * draws a piece of the weapon crosshair. */
static inline int crosshair_site(uint32_t ret)
{
    switch (ret) {
    case 0x004CBBB4:   /* func_004CB798: the ring, then the middle */
        return 1;
    default:
        return 0;
    }
}

static inline double crosshair_clamp(double v)
{
    if (!(v == v)) return 1.0;   /* NaN: the game's size */
    return v < CROSSHAIR_MIN ? CROSSHAIR_MIN : v > CROSSHAIR_MAX ? CROSSHAIR_MAX : v;
}

/* The slider's position (0..1) to the size and back: the middle is 1, each
 * half runs linearly to its end, so the ends need not be symmetric. */
static inline double crosshair_from_slider(double p)
{
    if (!(p == p)) return 1.0;
    p = p < 0.0 ? 0.0 : p > 1.0 ? 1.0 : p;
    return p < 0.5 ? CROSSHAIR_MIN + (1.0 - CROSSHAIR_MIN) * (p / 0.5) : 1.0 + (CROSSHAIR_MAX - 1.0) * ((p - 0.5) / 0.5);
}
static inline double crosshair_to_slider(double s)
{
    s = crosshair_clamp(s);
    return s < 1.0 ? 0.5 * (s - CROSSHAIR_MIN) / (1.0 - CROSSHAIR_MIN) : 0.5 + 0.5 * (s - 1.0) / (CROSSHAIR_MAX - 1.0);
}

/* An offset from the screen's middle, in pixels, snapped so that an extent of
 * len pixels around it has whole-pixel edges (frac is 0 or 0.5: the fraction
 * those edges' offset must have), the same way on both sides of the middle. */
static inline double crosshair_snap(double off, double frac)
{
    const double a = fabs(off), s = floor(a - frac + 0.5) + frac;
    if (a == 0.0) return frac;   /* a piece in the very middle: half a pixel right/down when it must */
    return off < 0.0 ? -s : s;
}

/* One axis of a piece: lo..hi in the layout's units (1280x720 for the HUD,
 * already through the y scale and offset), the pixels per unit k, the layout's
 * middle mid_u and the output's size n (pixels). Writes the new lo..hi. */
static inline void crosshair_axis(double* lo, double* hi, double size, double k, double mid_u, double n)
{
    const int flip = *hi < *lo;
    const double a = flip ? *hi : *lo, b = flip ? *lo : *hi;
    double len = floor((b - a) * k * size + 0.5);
    if (len < 1.0) len = 1.0;
    const double mid_px = n * 0.5;
    /* the fraction the piece's middle must have so that its edges fall on
     * whole pixels: 0 for an even length on an even-sized screen */
    const double f = fmod(len * 0.5 + mid_px, 1.0);
    const double c = crosshair_snap(((a + b) * 0.5 - mid_u) * k, f);
    const double na = mid_u + (c - len * 0.5) / k, nb = mid_u + (c + len * 0.5) / k;
    *lo = flip ? nb : na;
    *hi = flip ? na : nb;
}

/* A crosshair piece's rectangle x0 y0 x1 y1 (the layout's units, the y ones
 * on screen: through the quad's y scale and offset), for the setting's size,
 * on an output of w x h pixels showing a 2D layer of layer_w x layer_h units
 * (1280 x widening by 720), centred in the output. Size 1: left as it is. */
static inline void crosshair_rect(double r[4], double size, double w, double h, double layer_w, double layer_h)
{
    size = crosshair_clamp(size);
    if (size == 1.0 || !(w > 0.0) || !(h > 0.0)) return;
    const double kx = w / layer_w, ky = h / layer_h, k = kx < ky ? kx : ky;   /* the layer keeps its shape */
    crosshair_axis(&r[0], &r[2], size, k, layer_w * 0.5, w);
    crosshair_axis(&r[1], &r[3], size, k, layer_h * 0.5, h);
}
#endif
