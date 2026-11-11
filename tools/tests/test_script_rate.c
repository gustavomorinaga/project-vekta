/* The mission scripts' OnUpdate rate (port/kz1hd/script_rate.h): once per 1/30 s of game time whatever the game
 * clock's tick rate and the frame rate. Before, it ran once per frame: at 60 or 144 ticks a second the platform
 * ride of 11.SDDockingBay (0.34 per OnUpdate over 502.3, about 49 s on the PS3) took about 12 s and the section's
 * end never came. The frames here come as func_0021C0F8 makes them: each advances 1 tick or more. */
#include <stdio.h>
#include "script_rate.h"

static int fails;
static void check(const char* what, double got, double want, double tol)
{
    const double d = got > want ? got - want : want - got;
    if (d > tol) { fprintf(stderr, "FAIL: %s = %.3f, want %.3f\n", what, got, want); fails++; }
    else printf("ok   %s = %.3f\n", what, got);
}

/* Game seconds the SDDockingBay platform takes: frames advance `per_frame` ticks at `hz` from tick `start`. */
static double ride_seconds(int hz, int per_frame, uint32_t start)
{
    int64_t last = SCRIPT_RATE_NONE;
    double x = 0;
    uint32_t t = start;
    for (long frame = 0; frame < 10000000; frame++) {
        t += (uint32_t)per_frame;
        if (!script_rate_due(t, hz, &last)) continue;
        if (!(x < 502.3f)) return (double)(t - start) / hz;
        x = (float)(x + 0.34f);
    }
    return -1;
}

/* OnUpdate calls in `seconds` of game time, frames of `per_frame` ticks */
static long calls(int hz, int per_frame, int seconds)
{
    int64_t last = SCRIPT_RATE_NONE;
    long n = 0;
    for (uint32_t t = 0; t < (uint32_t)(hz * seconds); ) { t += (uint32_t)per_frame; n += script_rate_due(t, hz, &last); }
    return n;
}

int main(void)
{
    /* the clock's rate from its seconds per tick (+0x54) */
    check("hz 1/30", script_rate_clock_hz(1.0f / 30), 30, 0);
    check("hz 1/60", script_rate_clock_hz(1.0f / 60), 60, 0);
    check("hz 1/144", script_rate_clock_hz(1.0f / 144), 144, 0);
    check("hz 0x3D088889 (the game's 1/30)", script_rate_clock_hz(0.033333335f), 30, 0);
    check("hz 0", script_rate_clock_hz(0.0f), 30, 0);
    check("hz -1", script_rate_clock_hz(-1.0f), 30, 0);
    check("hz 5", script_rate_clock_hz(5.0f), 30, 0);

    /* 30 a second at every clock rate, one tick per frame (the frame rate at the clock rate) */
    check("calls/s at 30", calls(30, 1, 100) / 100.0, 30, 0);
    check("calls/s at 60", calls(60, 1, 100) / 100.0, 30, 0.02);
    check("calls/s at 120", calls(120, 1, 100) / 100.0, 30, 0.02);
    check("calls/s at 144", calls(144, 1, 100) / 100.0, 30, 0.02);
    check("calls/s at 240", calls(240, 1, 100) / 100.0, 30, 0.02);
    /* at 30 every frame calls, as the game does, even a frame of 2 ticks (a slow frame: 15 calls a second) */
    check("calls/s at 30, 2 ticks a frame", calls(30, 2, 100) / 100.0, 15, 0);
    /* at 144 with frames of 3 ticks (48 fps): still 30 a second */
    check("calls/s at 144, 3 ticks a frame", calls(144, 3, 100) / 100.0, 30, 0.02);
    /* never more than one call a frame: frames of 10 ticks at 144 (14.4 fps) */
    check("calls/s at 144, 10 ticks a frame", calls(144, 10, 100) / 100.0, 14.4, 0.01);

    /* the platform ride: about 49 s of game time at 30, 60 and 144 (it was 24.6 s at 60 and 10.3 s at 144) */
    const double at30 = ride_seconds(30, 1, 0);
    check("ride at 30", at30, 1478.0 / 30, 0.05);
    check("ride at 60", ride_seconds(60, 1, 0), at30, 0.05);
    check("ride at 144", ride_seconds(144, 1, 0), at30, 0.05);
    check("ride at 144, 2 ticks a frame", ride_seconds(144, 2, 12345), at30, 0.05);
    check("ride at 144, a large tick count", ride_seconds(144, 1, 0xFFFF0000u - 144 * 60), at30, 0.05);

    /* a new clock (the tick count goes back) calls at once */
    int64_t last = SCRIPT_RATE_NONE;
    script_rate_due(144 * 100, 144, &last);
    check("count went back", script_rate_due(10, 144, &last), 1, 0);
    check("same step again", script_rate_due(11, 144, &last), 0, 0);

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
