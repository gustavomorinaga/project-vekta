/* The rates behind the frame-rate limit and the mouse look (port/kz1hd/pc_rates.h, #121): below 100 fps the flip
 * cadence is a whole number of flips per game tick, so every frame is shown for the same time, and the mouse turns
 * pitch the same degrees per count as yaw. */
#include <math.h>
#include <stdio.h>
#include "pc_rates.h"

int main(void)
{
    int fails = 0;
#define EXPECT(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)
    static const int limits[] = { 30, 60 };
    for (int i = 0; i < 2; i++) {
        const double r = pc_flip_hz(limits[i]) / pc_tick_hz(limits[i]);
        char what[96];
        snprintf(what, sizeof what, "limit %d: %.3f flips per game tick, a whole number", limits[i], r);
        EXPECT(fabs(r - round(r)) < 0.01 && round(r) >= 1, what);
    }
    EXPECT(pc_tick_hz(144) == 144 && pc_flip_hz(144) == 144.0 && pc_guest_vblank_hz(144) == 144.0,
           "limit 144: clock, cadence and the game's vblanks at 144, as before");
    EXPECT(pc_guest_vblank_hz(60) == 59.94 && pc_guest_vblank_hz(30) == 59.94, "below 100 the game sees the PS3's 59.94 vblanks");
    EXPECT(pc_tick_hz(30) == 30 && pc_tick_hz(60) == 60, "the game clock follows the limit");
    {   /* the higher limits: clock and the game's vblanks at the limit, a 144 Hz cadence in menus (raised in play) */
        static const int hi[] = { 120, 144, 165, 240 };
        int good = 1;
        for (int i = 0; i < 4; i++)
            good &= pc_tick_hz(hi[i]) == hi[i] && pc_guest_vblank_hz(hi[i]) == (double)hi[i] && pc_flip_hz(hi[i]) == 144.0;
        EXPECT(good, "limits 120, 144, 165, 240: clock and vblanks at the limit, cadence 144");
    }
    EXPECT(pc_tick_hz(0) == 1000 && pc_guest_vblank_hz(0) == 1000.0 && pc_flip_hz(0) == 144.0,
           "Unlimited (0): a 1000 Hz clock, a tick per ms of the game's timer");
    EXPECT(pc_tick_hz(5000) == 1000 && pc_tick_hz(-1) == 30, "an odd value: at most 1000, below 0 the game's 30");

    const double pitch = KZ_LOOK_PITCH_DEG_PER_COUNT * KZ_LOOK_PITCH_SCALE;
    char what[96];
    snprintf(what, sizeof what, "mouse: pitch %.5f deg/count over yaw %.5f = %.4f, within 1 +- 3%%", pitch,
             KZ_LOOK_YAW_DEG_PER_COUNT, pitch / KZ_LOOK_YAW_DEG_PER_COUNT);
    EXPECT(fabs(pitch / KZ_LOOK_YAW_DEG_PER_COUNT - 1.0) <= 0.03, what);
    EXPECT(fabs(KZ_LOOK_K - 0.022 * 3.14159265 / 180.0 / 2.27) < 1e-9, "mouse: yaw's constant is 0.7.0's");

    /* Frames of 1, 2, 0, 3 and 1 ticks with counts in each: the game applies a frame's rate once per tick, so the
     * turn is rate x ticks; the total must be the counts, none doubled and none lost (the 0-tick frame's are kept). */
    {
        static const unsigned steps[] = { 1, 2, 0, 3, 1, 1 };
        static const int counts[] = { 10, 400, 7, 30, -5, 0 };
        struct pc_look_carry c = { 0, 0, 0, 0 };
        unsigned now = 1000; double turn = 0; int given = 0, held = 0;
        pc_look_frame(&c, now, 60, &(int){0}, &(int){0});   /* primes */
        for (int i = 0; i < 6; i++) {
            now += steps[i];
            int dx = counts[i], dy = 0;
            given += counts[i];
            const float per = pc_look_frame(&c, now, 60, &dx, &dy);
            turn += (double)dx * per * steps[i];
            if (!steps[i]) held = c.x == counts[i] && per == 0.0f;
        }
        char w[96];
        snprintf(w, sizeof w, "mouse: frames of 1,2,0,3,1 ticks turn %.3f counts' worth of %d", turn, given);
        EXPECT(fabs(turn - given) < 1e-3, w);
        EXPECT(held, "mouse: a frame with no tick keeps its counts for the next");
        int dx = 50, dy = 0;
        EXPECT(pc_look_frame(&c, now + 5000, 60, &dx, &dy) == 1.0f && dx == 50, "mouse: a jump of the clock (a load) counts as one tick");
    }
    /* the jump limit: 8 ticks up to 144 Hz (unchanged), 1/8 s above; an Unlimited (1000 Hz) frame of 10 ticks
     * (~100 fps) is divided, not taken for a load */
    EXPECT(pc_look_max_jump(30) == 8 && pc_look_max_jump(60) == 8 && pc_look_max_jump(144) == 8,
           "mouse: up to 144 Hz a jump is more than 8 ticks, as before");
    EXPECT(pc_look_max_jump(165) == 20 && pc_look_max_jump(240) == 30 && pc_look_max_jump(1000) == 125,
           "mouse: above 144 Hz a jump is more than 1/8 s of ticks");
    {
        struct pc_look_carry c = { 0, 0, 0, 0 };
        unsigned now = 5000;
        pc_look_frame(&c, now, 1000, &(int){0}, &(int){0});
        int dx = 100, dy = 0;
        const float per = pc_look_frame(&c, now + 10, 1000, &dx, &dy);
        EXPECT(per == 0.1f && dx == 100, "mouse: at 1000 Hz a 10-tick frame gets 1/10 of the rate per tick");
        dx = 40; dy = 0;
        EXPECT(pc_look_frame(&c, now + 10 + 400, 1000, &dx, &dy) == 1.0f, "mouse: at 1000 Hz a 400 ms jump counts as one tick");
    }
    if (fails) return 1;
    puts("test_pc_rates: ok");
    return 0;
}
