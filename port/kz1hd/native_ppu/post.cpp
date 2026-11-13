/*
 * The screen effects' strengths (post.h, pcoptions.cpp). See native_ppu.h.
 *
 * The camera's screen-effects object (camera +0x108, r3 here) holds the
 * values the game draws its full-screen effects with; func_0021AB28 calls
 * these two for it every frame (0x21B664 and 0x21B858). What the object
 * holds there, read from the lifted bodies:
 *
 *   +0x78  the motion field's resource, +0x7C its factor (type 5 channel)
 *   +0xD0, +0xD4  the Colorize pass's resources (both needed)
 *   +0x1B0 contrast, +0x1B4 brightness, +0x1B8 mix factor
 *   +0x1C0..+0x1C8, +0x1D0..+0x1D8, +0x1E0..+0x1E8  shadow, midtone and
 *          highlight colours (RGB, times 255 as they are set)
 *   +0x1F0 the grain's blend value (func_0004C8F8), +0x1F4 the grain's
 *          alpha, +0x1F8 the grain's size, +0x1FC its texture
 *   +0x204 the effects on (byte), +0x208 the Colorize pass object
 *
 * func_0029BDC8 sets the Colorize pass from +0x1B0..+0x1E8 and draws it, then
 * draws the motion field into the 640x360 target IO 0xF1000 through
 * func_001106C4 (the post singleton at 0x8285A0, factor +0x7C) for jm_post's
 * MotionBlur (or the GPU pass, ps3recomp patch 2043). func_0029C030 draws the
 * film grain: tiles of 32 of the texture at +0x1FC, with random offsets
 * (func_00013B48), coloured with alpha int(+0x1F4 * 255), none below 2/255.
 *
 * Each wrapper scales its value for the call and puts the game's back after
 * (unless the game wrote another one meanwhile). The Colorize values are
 * never touched: colour grading stays the game's.
 */
#include "native_ppu.h"
#include "../post.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern "C" volatile int kz_post_pct[KZ_POST_COUNT] = { KZ_POST_FULL, KZ_POST_FULL, KZ_POST_FULL };

namespace {

/* KZ_POST_LOG=1 (default off): about once a second, the game's value and the
 * one drawn with, per effect ([kz-post]). */
bool log_on()
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("KZ_POST_LOG"); on = e && atoi(e) ? 1 : 0; }
    return on;
}
void log_value(const char* what, uint32_t fx, float game, float drawn, int pct)
{
    static time_t last[2];
    const int k = what[0] == 'm' ? 0 : 1;
    const time_t t = time(nullptr);
    if (t == last[k]) return;
    last[k] = t;
    fprintf(stderr, "[kz-post] %s fx 0x%08X on %u game %.6g drawn %.6g (%d %%)\n", what, fx,
            fx ? (unsigned)vm_read8(fx + 0x204) : 0u, game, drawn, pct);
}

/* Runs `lifted` with the float at `ea` (in the effects object `fx`) scaled by
 * `pct`; at 100 just runs it. */
void scaled_call(ppu_context* ctx, const char* what, uint32_t fx, uint32_t ea, int pct, void (*lifted)(ppu_context*))
{
    if (log_on() && fx) { const uint32_t g = vm_read32(ea); float gv; memcpy(&gv, &g, 4);
                          log_value(what, fx, gv, kz_post_scale(gv, pct), pct); }
    if (pct >= KZ_POST_FULL || !fx) { lifted(ctx); return; }
    const uint32_t game = vm_read32(ea);
    float v;
    memcpy(&v, &game, 4);
    v = kz_post_scale(v, pct);
    uint32_t mine;
    memcpy(&mine, &v, 4);
    vm_write32(ea, mine);
    lifted(ctx);
    if (vm_read32(ea) == mine) vm_write32(ea, game);
}

}  // namespace

/* Colorize, then the motion field: the field's factor at +0x7C. */
void func_0029BDC8(ppu_context* ctx)
{
    const uint32_t fx = (uint32_t)ctx->gpr[3];
    scaled_call(ctx, "motion_blur", fx, fx + 0x7C, kz_post_pct[KZ_POST_MOTION_BLUR], func_0029BDC8_lifted);
}

/* The film grain: its alpha at +0x1F4. */
void func_0029C030(ppu_context* ctx)
{
    const uint32_t fx = (uint32_t)ctx->gpr[3];
    scaled_call(ctx, "film_grain", fx, fx + 0x1F4, kz_post_pct[KZ_POST_FILM_GRAIN], func_0029C030_lifted);
}
