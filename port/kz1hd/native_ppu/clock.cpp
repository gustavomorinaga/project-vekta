/*
 * The game clock's frame step, for routes (../route.cpp). See native_ppu.h.
 *
 * func_0021C0F8(clock r3) starts each frame of the main loop: it waits until
 * at least one whole tick has passed since the last frame, then leaves the
 * ticks this frame advances (1 up to the cap at +0x74) at +0x68, the tick
 * count at +0x6C and the previous one at +0x70. Ticks are the millisecond
 * timer (func_0001CC20) times the double at 0x21C0F0. When +0x64 is 1 it then
 * calls func_0021BDF8(f1, f2, clock) with the floats at 0x544E90/0x544E94.
 *
 * While a route is replayed the frame takes the recorded ticks instead,
 * without waiting: the game advances the same game time per frame as when
 * it was recorded, however fast or slow the host draws.
 */
#include "native_ppu.h"
#include "../route.h"

extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#ifdef KZ_TRACY
#include "tracy/TracyC.h"
#endif

void func_0021C0F8(ppu_context* ctx)
{
    const uint32_t clk = (uint32_t)ctx->gpr[3];
    int32_t n, now, prev;
#ifdef KZ_TRACY
    TracyCFrameMarkNamed("game frame");   /* the game's main loop starts a frame */
#endif
    if (!kz_route_clock(clk, &n, &now, &prev)) {
#ifdef KZ_TRACY
        /* the game's own clock: waits for at least one whole tick */
        TracyCZoneN(tz, "frame clock", 1);
        func_0021C0F8_lifted(ctx);
        TracyCZoneEnd(tz);
#else
        func_0021C0F8_lifted(ctx);
#endif
        kz_route_clock_done(clk);
        return;
    }
    vm_write32(clk + 0x70, (uint32_t)prev);
    vm_write32(clk + 0x6C, (uint32_t)now);
    vm_write32(clk + 0x68, (uint32_t)n);
    if ((int32_t)vm_read32(clk + 0x64) == 1) {
        const uint64_t sp = ctx->gpr[1], lr = ctx->lr;
        vm_write64(sp - 0xA0, sp);
        ctx->gpr[1] = sp - 0xA0;
        const double a = ldf(0x544E90), b = ldf(0x544E94);
        ctx->fpr[1] = a; ctx->fpr[3] = b; ctx->fpr[2] = S(a + b);
        ctx->gpr[3] = clk; ctx->gpr[4] = ctx->gpr[5] = 0x540000;
        ctx->lr = 0x0021C1B4; func_0021BDF8(ctx);
        while (g_trampoline_fn) { void (*t)(void*) = g_trampoline_fn; g_trampoline_fn = 0; t(ctx); }
        ctx->gpr[1] = sp; ctx->lr = lr;
    }
}
