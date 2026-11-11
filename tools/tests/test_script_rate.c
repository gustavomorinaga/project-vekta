/* The mission scripts' OnUpdate rate (port/kz1hd/script_rate.h): once per 1/30 s of game time whatever the game
 * clock's tick rate and the frame rate, and on any frame with script messages waiting (the main loop drops them after
 * OnUpdate). Before, OnUpdate ran once per frame: at 60 or 144 ticks a second the platform ride of 11.SDDockingBay
 * (0.34 per OnUpdate over 502.3, about 49 s on the PS3) took about 12 s and the section's end never came. A first fix
 * that only skipped frames lost the messages of the skipped ones (a gate that never opened, enemies that never came).
 * The frames here come as func_0021C0F8 makes them: each advances 1 tick or more. */
#include <stdio.h>
#include "script_rate.h"

static int fails;
static void check(const char* what, double got, double want, double tol)
{
    const double d = got > want ? got - want : want - got;
    if (d > tol) { fprintf(stderr, "FAIL: %s = %.3f, want %.3f\n", what, got, want); fails++; }
    else printf("ok   %s = %.3f\n", what, got);
}

/* Game seconds the SDDockingBay platform takes: frames advance `per_frame` ticks at `hz` from tick `start`; a
 * message waits on every `msg_every`-th frame (0: never). */
static double ride_seconds(int hz, int per_frame, uint32_t start, int msg_every)
{
    struct script_rate s = {0};
    double x = 0;
    uint32_t t = start;
    for (long frame = 1; frame < 10000000; frame++) {
        t += (uint32_t)per_frame;
        if (!script_rate_due(&s, t, hz, msg_every && frame % msg_every == 0)) continue;
        if (!(x < 502.3f)) return (double)(t - start) / hz;
        x = (float)(x + 0.34f);
    }
    return -1;
}

/* OnUpdate calls per second over `seconds` of game time, frames of `per_frame` ticks, a message every
 * `msg_every`-th frame; *lost counts messages on frames without a call */
static double rate(int hz, int per_frame, int seconds, int msg_every, long* lost)
{
    struct script_rate s = {0};
    long n = 0, frame = 0;
    if (lost) *lost = 0;
    for (uint32_t t = 0; t < (uint32_t)(hz * seconds); ) {
        t += (uint32_t)per_frame; frame++;
        const int msg = msg_every && frame % msg_every == 0;
        const int due = script_rate_due(&s, t, hz, msg);
        n += due;
        if (msg && !due && lost) ++*lost;
    }
    return (double)n / seconds;
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
    check("calls/s at 30", rate(30, 1, 100, 0, 0), 30, 0);
    check("calls/s at 60", rate(60, 1, 100, 0, 0), 30, 0.02);
    check("calls/s at 120", rate(120, 1, 100, 0, 0), 30, 0.02);
    check("calls/s at 144", rate(144, 1, 100, 0, 0), 30, 0.02);
    check("calls/s at 240", rate(240, 1, 100, 0, 0), 30, 0.02);
    /* at 30 every frame calls, as the game does, even a frame of 2 ticks (a slow frame: 15 calls a second) */
    check("calls/s at 30, 2 ticks a frame", rate(30, 2, 100, 0, 0), 15, 0);
    /* at 144 with frames of 3 ticks (48 fps): still 30 a second */
    check("calls/s at 144, 3 ticks a frame", rate(144, 3, 100, 0, 0), 30, 0.02);
    /* never more than one call a frame and no catching up: frames of 10 ticks at 144 (14.4 fps) */
    check("calls/s at 144, 10 ticks a frame", rate(144, 10, 100, 0, 0), 14.4, 0.01);

    /* messages are never dropped and the rate stays 30 */
    long lost;
    check("calls/s at 144, a message every 7th frame", rate(144, 1, 100, 7, &lost), 30, 0.1);
    check("  messages lost", lost, 0, 0);
    check("calls/s at 144, a message every 50th frame", rate(144, 1, 100, 50, &lost), 30, 0.1);
    check("  messages lost", lost, 0, 0);
    check("calls/s at 60, a message every 3rd frame", rate(60, 1, 100, 3, &lost), 30, 0.1);
    check("  messages lost", lost, 0, 0);
    /* a message every frame: every frame calls (the debt is forgiven), nothing lost */
    check("calls/s at 144, a message every frame", rate(144, 1, 10, 1, &lost), 144, 0.01);
    check("  messages lost", lost, 0, 0);

    /* the platform ride: about 49 s of game time at 30, 60 and 144 (it was 24.6 s at 60 and 10.3 s at 144) */
    const double at30 = ride_seconds(30, 1, 0, 0);
    check("ride at 30", at30, 1478.0 / 30, 0.05);
    check("ride at 60", ride_seconds(60, 1, 0, 0), at30, 0.05);
    check("ride at 144", ride_seconds(144, 1, 0, 0), at30, 0.05);
    check("ride at 144, 2 ticks a frame", ride_seconds(144, 2, 12345, 0), at30, 0.05);
    check("ride at 144, a large tick count", ride_seconds(144, 1, 0xFFFF0000u - 144 * 60, 0), at30, 0.05);
    check("ride at 144, a message every 20th frame", ride_seconds(144, 1, 0, 20), at30, 0.1);

    /* a new clock (the tick count goes back) calls at once */
    struct script_rate s = {0};
    script_rate_due(&s, 144 * 100, 144, 0);
    check("count went back", script_rate_due(&s, 10, 144, 0), 1, 0);
    check("same step again", script_rate_due(&s, 11, 144, 0), 0, 0);
    check("same step, a message", script_rate_due(&s, 12, 144, 1), 1, 0);

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("all passed\n");
    return 0;
}
