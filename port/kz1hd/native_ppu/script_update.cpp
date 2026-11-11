/*
 * The mission scripts' OnUpdate at the game's 30 a second (../script_rate.h).
 *
 * func_00069A3C(r3 script state + 8, r4 function name) calls a Lua function
 * of a script by name (through func_00069610 with no arguments). The main
 * loop func_0021AB28 calls it with "OnUpdate" (0x5436D4) for the scripts at
 * +0x164 and +0x160 of the game object (*0x828734), once per frame (returns
 * to 0x21AE1C and 0x21AE38). Those two calls are let through once per 1/30 s
 * of game time, and on any frame with script messages waiting (the main loop
 * drops them after OnUpdate); every other call goes to the lifted body
 * unchanged. The callers do not use what it returns.
 *
 * KZ_SCRIPT_EVERY_FRAME=1 (default 0): OnUpdate every frame again, as before
 * this fix (scripted events then run at the frame rate's pace above 30 fps).
 *
 * func_004940B0(r3 object, f1..f3 x, y, z) is the objects' SetTranslation:
 * it stores the position at +0x50..+0x58 and calls the object's virtual at
 * vtable +0x5C (the transform changed). Calls made inside those OnUpdates
 * go through script_smooth.h, and every frame slides the objects it holds.
 * KZ_SCRIPT_SMOOTH=0 (default 1): objects jump once per 1/30 s instead.
 */
#include "native_ppu.h"
#include "../script_rate.h"
#include "../script_smooth.h"
#include <stdio.h>
#include <stdlib.h>

extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);

static struct smooth_state s_smooth;
static int s_in_update;     /* inside a gated OnUpdate: its SetTranslation calls are smoothed */
static int64_t s_step = -1; /* the current 1/30 s step, while smoothing applies */
static float s_frac;        /* how far through it */

static int smooth_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("KZ_SCRIPT_SMOOTH"); on = !(e && *e == '0'); }
    return on;
}

/* SetTranslation through the lifted body, the caller's registers kept */
static void set_translation(ppu_context* ctx, uint32_t obj, const float p[3])
{
    const uint64_t r3 = ctx->gpr[3], r4 = ctx->gpr[4], lr = ctx->lr;
    const double f1 = ctx->fpr[1], f2 = ctx->fpr[2], f3 = ctx->fpr[3];
    ctx->gpr[3] = obj; ctx->fpr[1] = p[0]; ctx->fpr[2] = p[1]; ctx->fpr[3] = p[2];
    ctx->lr = 0x00069A3C;   /* any guest address: the body returns to the host */
    func_004940B0_lifted(ctx);
    while (g_trampoline_fn) { void (*t)(void*) = g_trampoline_fn; g_trampoline_fn = 0; t(ctx); }
    ctx->gpr[3] = r3; ctx->gpr[4] = r4; ctx->lr = lr; ctx->fpr[1] = f1; ctx->fpr[2] = f2; ctx->fpr[3] = f3;
}

static void smooth_move(uint32_t obj, const float p[3], void* user)
{
    set_translation((ppu_context*)user, obj, p);
}

void func_004940B0(ppu_context* ctx)
{
    if (!s_in_update || s_step < 0) { func_004940B0_lifted(ctx); return; }
    const float p[3] = {(float)ctx->fpr[1], (float)ctx->fpr[2], (float)ctx->fpr[3]};
    float out[3];
    smooth_set(&s_smooth, (uint32_t)ctx->gpr[3], p, s_step, s_frac, out);
    ctx->fpr[1] = out[0]; ctx->fpr[2] = out[1]; ctx->fpr[3] = out[2];
    func_004940B0_lifted(ctx);
}

void func_00069A3C(ppu_context* ctx)
{
    static int every_frame = -1;
    static struct script_rate rate[2];
    const int site = ctx->lr == 0x0021AE1Cu ? 0 : ctx->lr == 0x0021AE38u ? 1 : -1;
    if (site >= 0 && (uint32_t)ctx->gpr[4] == 0x005436D4u) {
        if (every_frame < 0) { const char* e = getenv("KZ_SCRIPT_EVERY_FRAME"); every_frame = e && *e && *e != '0'; }
        const uint32_t game = vm_read32(0x828734);   /* the main loop's object, with the game clock (main.cpp) */
        const bool ok = game && game < 0xF0000000u;
        if (!every_frame && ok) {
            const int hz = script_rate_clock_hz((float)ldf(game + 0x54));
            const uint32_t q = vm_read32(game + 0xB4);   /* the script message queue */
            const int pending = q && q < 0xF0000000u && (int32_t)vm_read32(q + 0x8) > 0;
            const uint32_t ticks = vm_read32(game + 0x6C);
            if (site == 0) {   /* once a frame: slide the objects of the last OnUpdates */
                const int64_t step = (int64_t)ticks * SCRIPT_RATE_HZ / hz;
                if (hz <= SCRIPT_RATE_HZ || !smooth_on()) { s_step = -1; smooth_reset(&s_smooth); }
                else {
                    if (step < s_step) smooth_reset(&s_smooth);   /* a new clock */
                    s_step = step;
                    s_frac = (float)((int64_t)ticks * SCRIPT_RATE_HZ - step * hz) / (float)hz;
                    smooth_frame(&s_smooth, step, s_frac, smooth_move, ctx);
                }
            }
            if (!script_rate_due(&rate[site], ticks, hz, pending)) return;
            static int said;
            if (hz > SCRIPT_RATE_HZ && !said++)
                fprintf(stderr, "[kz] mission scripts' OnUpdate at %d a second of game time (clock at %d Hz)\n",
                        SCRIPT_RATE_HZ, hz);
        }
        /* KZ_SCRIPT_LOG=1 (default 0): every 300 OnUpdate calls of the first site, the calls per second of game
         * time since the previous line (30 on the PS3) */
        static int log = -1;
        if (log < 0) { const char* e = getenv("KZ_SCRIPT_LOG"); log = e && *e && *e != '0'; }
        if (log && site == 0 && ok) {
            static uint32_t n, t0;
            const uint32_t t = vm_read32(game + 0x6C);
            const int hz = script_rate_clock_hz((float)ldf(game + 0x54));
            if (n == 0 || t < t0) { t0 = t; n = 0; }
            if (++n == 301) {
                fprintf(stderr, "[kz-script] 300 OnUpdate in %.2f s of game time: %.1f a second (clock %d Hz)\n",
                        (double)(t - t0) / hz, 300.0 * hz / (double)(t - t0 ? t - t0 : 1), hz);
                n = 1; t0 = t;
            }
        }
    }
    if (site >= 0 && (uint32_t)ctx->gpr[4] == 0x005436D4u) {
        s_in_update = 1;
        func_00069A3C_lifted(ctx);
        s_in_update = 0;
        return;
    }
    func_00069A3C_lifted(ctx);
}
