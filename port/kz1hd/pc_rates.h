/*
 * The rates behind two PC settings (pcoptions.cpp): the game clock and flip
 * cadence for a frame-rate limit, and the mouse look's per-axis scale.
 * Header-only, plain C, so a host test can check them
 * (tools/tests/test_pc_rates.c).
 *
 * Frame-rate limit. The game clock (KZ_TICK_HZ, main.cpp) runs at the limit;
 * the game sees the PS3's 59.94 vblanks below 100 (part of its logic counts
 * them); the flip cadence (KZ_VBLANK_HZ) is the rate the FIFO drains one
 * frame per tick of. Below 100 the cadence is the PS3's 59.94 in every V-Sync
 * mode, a whole number of ticks per game frame: on a 144 Hz cadence a 60 Hz
 * game came out as frames 2 and 3 ticks apart, 13.9 and 20.8 ms (#121,
 * measured 2026-11-24 with V-Sync off: 1% low 47.6 fps at the 60 limit), and
 * on a VRR display the monitor swung between 72 and 144 Hz. From 100 up (120,
 * 144, 165, 240) the clock and the game's vblanks run at the limit, the
 * cadence is 144 in menus and menu_mouse raises it in play (pcoptions.cpp).
 * 0 is Unlimited: the clock at 1000 Hz, one tick per millisecond of the
 * game's own timer (func_0021C0F8 counts whole ms), its finest; the frame rate
 * is then the PC's. ASSUMPTION: the game's vblank-counting logic takes 1000 a
 * second as it takes 144 (checked in play, not in the code).
 *
 * Mouse look. player_look adds the counts times one constant to the
 * controller's turn rates, +0xA8 (yaw) and +0xAC (pitch), after the game's
 * func_00291FA8. func_002E1548 turns both rates into radians alike (x pi/180,
 * the double at 0x2E1540), but the turn that follows is not the same per axis:
 * measured 2026-11-24 at slider 0.5 (1x), in Level 1, the same at 60 and 144
 * Hz (#121, wave 1), the one constant gave
 *     yaw   0.04365 deg/count
 *     pitch 0.01184 deg/count (0.271 of yaw)
 * The pitch angle (player+0x14C) is written by func_002DE680 from its
 * accumulator at player+0x154; where the factor between the axes comes from
 * was not found in the code (a read of *(player+8) +0xB0, a divisor on one of
 * func_002DE680's paths, gave no usable float in play). The yaw constant stays
 * as in 0.7.0; pitch gets it times KZ_LOOK_PITCH_SCALE, so both axes turn the
 * same degrees per count. ASSUMPTION: the scale is the measured ratio.
 */
#ifndef KZ_PC_RATES_H
#define KZ_PC_RATES_H

/* the game clock for a limit: 30 is the game's own, 0 Unlimited */
static inline int pc_tick_hz(int fps_limit)
{
    return fps_limit == 0 ? 1000 : fps_limit >= 100 ? (fps_limit > 1000 ? 1000 : fps_limit) : fps_limit <= 30 ? 30 : 60;
}
/* the vblanks the game sees (KZ_GUEST_VBLANK_HZ): its clock's from 100 up */
static inline double pc_guest_vblank_hz(int fps_limit) { return pc_tick_hz(fps_limit) >= 100 ? (double)pc_tick_hz(fps_limit) : 59.94; }
/* the flip cadence (KZ_VBLANK_HZ), whatever the V-Sync mode: 144 in menus from 100 up (raised in play) */
static inline double pc_flip_hz(int fps_limit) { return pc_tick_hz(fps_limit) >= 100 ? 144.0 : 59.94; }

/* The look constant in rate units per count per second (times the tick rate
 * per tick, times the slider's multiplier). The number is 0.7.0's, written as
 * 0.022 deg/count at an assumed 2.27 rad/s per unit; the game turns yaw at
 * about 4.50 rad/s per unit and pitch at 1.22, hence the measured rates above. */
#define KZ_LOOK_K (0.022f * 3.14159265f / 180.0f / 2.27f)
#define KZ_LOOK_YAW_DEG_PER_COUNT 0.04365     /* measured with KZ_LOOK_K */
#define KZ_LOOK_PITCH_DEG_PER_COUNT 0.01184   /* measured with KZ_LOOK_K, before the scale */
#define KZ_LOOK_PITCH_SCALE 3.687f            /* 0.04365 / 0.01184 */

/* The game applies one frame's turn rates once per tick of the game clock the
 * frame advances, so a frame of 2 ticks (a dip below the clock's rate, often
 * at 144) turned the mouse's counts twice (#121: a 400-count pitch burst
 * turned 34.92 deg instead of 17.46 on a 2-tick step, 2026-11-25). Per frame:
 * the counts get 1/ticks of the per-tick rate, and a frame that advances no
 * tick keeps them for the next, so no count is lost or doubled. `now` is the
 * clock's tick count; a jump of more than pc_look_max_jump(hz) ticks (a new
 * clock, a load) counts as one tick: 8 up to 144 Hz, as before; above, 1/8 s
 * of ticks, since at 240 or Unlimited (1000) an ordinary frame advances 2 to
 * 15 ticks and must still be divided. Returns the rate's scale, 0 when the
 * counts were kept.
 * ASSUMPTION: +0x6C already holds this frame's ticks when the look runs
 * (checked in the game by bursts on 2-tick frames, not in the code). */
struct pc_look_carry { int x, y; unsigned last; int primed; };
static inline unsigned pc_look_max_jump(int hz) { return hz <= 144 ? 8u : (unsigned)hz / 8u; }
static inline float pc_look_frame(struct pc_look_carry* c, unsigned now, int hz, int* dx, int* dy)
{
    unsigned n = c->primed ? now - c->last : 1;
    if (n > pc_look_max_jump(hz)) n = 1;
    c->last = now; c->primed = 1;
    *dx += c->x; *dy += c->y; c->x = c->y = 0;
    if (!n) { c->x = *dx; c->y = *dy; *dx = *dy = 0; return 0.0f; }
    return 1.0f / (float)n;
}

#endif
