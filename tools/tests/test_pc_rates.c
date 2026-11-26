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
        pc_look_frame(&c, now, &(int){0}, &(int){0});   /* primes */
        for (int i = 0; i < 6; i++) {
            now += steps[i];
            int dx = counts[i], dy = 0;
            given += counts[i];
            const float per = pc_look_frame(&c, now, &dx, &dy);
            turn += (double)dx * per * steps[i];
            if (!steps[i]) held = c.x == counts[i] && per == 0.0f;
        }
        char w[96];
        snprintf(w, sizeof w, "mouse: frames of 1,2,0,3,1 ticks turn %.3f counts' worth of %d", turn, given);
        EXPECT(fabs(turn - given) < 1e-3, w);
        EXPECT(held, "mouse: a frame with no tick keeps its counts for the next");
        int dx = 50, dy = 0;
        EXPECT(pc_look_frame(&c, now + 5000, &dx, &dy) == 1.0f && dx == 50, "mouse: a jump of the clock (a load) counts as one tick");
    }
    if (fails) return 1;
    puts("test_pc_rates: ok");
    return 0;
}
