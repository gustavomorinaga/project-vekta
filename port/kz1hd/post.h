/*
 * The post-processing strengths (pcoptions.cpp) and how the screen effects
 * take them (native_ppu/post.cpp). Header-only, plain C, so a host test can
 * check them (tools/tests/test_post.c).
 *
 * Each effect is a strength from 0 to 100 % in steps of 10. 100 is the game:
 * the parameter is left as the game set it, bit for bit. Below 100 the
 * parameter the game draws the effect with is scaled at its source, in the
 * screen-effects object the camera holds (+0x108, the r3 of func_0029BDC8 and
 * func_0029C030), for that one call, and put back after: nothing is skipped,
 * the game's own draws run with a smaller value. 0 is a value of 0, which the
 * game's own tests then treat as "no effect".
 *
 *   motion_blur = 0-100   the sprint's radial blur: the factor at +0x7C
 *                         (the type 5 channel, the player's sprint time
 *                         +0x310, set at 0x20148C), which func_0029BDC8
 *                         passes to func_001106C4 as motionFactor
 *   film_grain  = 0-100   the Noise: the alpha at +0x1F4 that
 *                         func_0029C030 draws its grain tiles with
 *                         (int(alpha * 255) as the colour's byte; nothing
 *                         below 2/255, the test at 0x29C008)
 *
 * The anti-aliasing (MLAA) is a choice, not a strength (antialiasing = mlaa
 * | off, rsx_vulkan_set_mlaa). The glow is drawn by the renderer (ps3recomp
 * patch 2078) and takes its strength in 256ths there:
 *
 *   glow        = 0-100   rsx_vulkan_set_glow_strength(kz_post_256(pct)),
 *                         256 the game's
 *
 * ENHANCEMENT: the PS3 game has no such settings.
 */
#ifndef KZ_POST_H
#define KZ_POST_H
#include <stdlib.h>
#include <string.h>

enum kz_post_effect {
    KZ_POST_MOTION_BLUR = 0,
    KZ_POST_FILM_GRAIN  = 1,
    KZ_POST_GLOW        = 2,   /* applied by the renderer, not in post.cpp */
    KZ_POST_COUNT
};

#define KZ_POST_STEP 10
#define KZ_POST_FULL 100   /* the game's own strength */

/* A setting's text to a strength: a whole number 0-100, clamped to that range
 * and rounded to the nearest step of 10 (5 rounds up); "on" is 100 and "off"
 * 0 (an older file's motion_blur = on | off). Anything else is `def`. */
static inline int kz_post_parse(const char* s, int def)
{
    if (!s) return def;
    if (!strcmp(s, "on")) return KZ_POST_FULL;
    if (!strcmp(s, "off")) return 0;
    char* end;
    const long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return def;
    if (v <= 0) return 0;
    if (v >= KZ_POST_FULL) return KZ_POST_FULL;
    return (int)((v + KZ_POST_STEP / 2) / KZ_POST_STEP * KZ_POST_STEP);
}

/* A menu row (0 = 0 %, 10 = 100 %) to a strength and back. */
static inline int kz_post_from_row(int row) { return (row < 0 ? 0 : row > 10 ? 10 : row) * KZ_POST_STEP; }
static inline int kz_post_row(int pct) { return (pct < 0 ? 0 : pct > KZ_POST_FULL ? KZ_POST_FULL : pct) / KZ_POST_STEP; }

/* A strength in 256ths, for a renderer switch that takes 0..256 with 256 the
 * game's (the GPU glow's rsx_vulkan_set_glow_strength): 100 % is exactly 256. */
static inline int kz_post_256(int pct) { return (pct <= 0 ? 0 : pct >= KZ_POST_FULL ? KZ_POST_FULL : pct) * 256 / KZ_POST_FULL; }

/* Scales one of the game's parameters: at 100 (or more) the very same bits,
 * else value * pct / 100 rounded to single as the game's floats are. */
static inline float kz_post_scale(float v, int pct)
{
    if (pct >= KZ_POST_FULL) return v;
    if (pct <= 0) return 0.0f;
    return (float)((double)v * (double)pct / (double)KZ_POST_FULL);
}
#endif
