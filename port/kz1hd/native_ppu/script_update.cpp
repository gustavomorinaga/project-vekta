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
 */
#include "native_ppu.h"
#include "../script_rate.h"
#include <stdio.h>
#include <stdlib.h>

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
            if (!script_rate_due(&rate[site], vm_read32(game + 0x6C), hz, pending)) return;
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
    func_00069A3C_lifted(ctx);
}
