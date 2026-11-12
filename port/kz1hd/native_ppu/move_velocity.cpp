/*
 * The player's movement record with the velocity of the game's 1/30 s ticks
 * (../move_vel.h has why).
 *
 * func_00294178(r3 the player controller) runs once a frame. Each call keeps
 * the player's position (player = controller +0x18, position +0x50..+0x58)
 * at the game clock's tick count (clock object at 0x828734, ticks +0x6C,
 * seconds per tick +0x54); above 30 ticks a second the player's velocity
 * (+0x80..+0x88) reads, during the call, as the change of position over the
 * last 1/30 s of game time, and is put back afterwards. Nothing else reads
 * that velocity meanwhile (the call runs on the game's main thread).
 *
 * KZ_MOVE_VEL30=0 (default 1): the velocity of the last tick, as before this
 * fix (the first-person weapon then shakes while the player slides along a
 * wall above 30 ticks a second).
 */
#include "native_ppu.h"
#include "../move_vel.h"
#include "../script_rate.h"
#include <stdlib.h>
#include <string.h>

static int move_vel_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("KZ_MOVE_VEL30"); on = !(e && *e == '0'); }
    return on;
}

void func_00294178(ppu_context* ctx)
{
    static struct move_vel s_hist;
    const uint32_t ctl = (uint32_t)ctx->gpr[3];
    const uint32_t clk = vm_read32(0x828734u);
    const uint32_t player = ctl ? vm_read32(ctl + 0x18) : 0;
    if (!move_vel_on() || !clk || !player || player >= 0x20000000u) { func_00294178_lifted(ctx); return; }
    const int hz = script_rate_clock_hz((float)ldf(clk + 0x54));
    const int64_t tick = (int64_t)(int32_t)vm_read32(clk + 0x6C);
    const float pos[3] = { (float)ldf(player + 0x50), (float)ldf(player + 0x54), (float)ldf(player + 0x58) };
    move_vel_add(&s_hist, player, tick, pos);
    float vel[3];
    if (!move_vel_window(&s_hist, hz, vel)) { func_00294178_lifted(ctx); return; }
    uint32_t saved[3], put[3];
    for (int k = 0; k < 3; k++) {
        saved[k] = vm_read32(player + 0x80 + 4 * k);
        memcpy(&put[k], &vel[k], 4);
        vm_write32(player + 0x80 + 4 * k, put[k]);
    }
    func_00294178_lifted(ctx);
    for (int k = 0; k < 3; k++)
        if (vm_read32(player + 0x80 + 4 * k) == put[k]) vm_write32(player + 0x80 + 4 * k, saved[k]);
}
