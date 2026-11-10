/*
 * The frame clock's wait without spinning (native_ppu/clock.cpp).
 *
 * func_0021C0F8 starts each frame of the game's main loop by waiting until a
 * whole tick has passed: it reads the millisecond timer (func_0001CC20) in a
 * loop, ticks = (int)(ms * factor) with the factor the double at 0x21C0F0, and
 * leaves the loop when the tick count moved past the one stored at clock
 * +0x6C. Measured 2026-11-10 (Level 7, the player standing still, the default
 * 60 Hz clock and 60 fps limit): 66.6 % of the main thread's CPU time was that
 * loop, and the main thread ~99 % busy at 60 fps.
 *
 * Before the loop runs, the native wrapper sleeps while more than a margin is
 * left until the next tick, a step of at most a millisecond at a time with
 * the timer read again after each step (so it stays right when guest time runs
 * faster than host time, PS3_TIMEBASE_SCALE); the lifted loop then spins the
 * rest and takes the tick exactly as before. Plain C so that a host test can
 * build it on its own (tools/tests/test_frame_wait.c).
 */
#ifndef KZ_FRAME_WAIT_H
#define KZ_FRAME_WAIT_H
#include <stdint.h>

/* Milliseconds of the game's timer left until (int)(ms * factor) passes last:
 * the next tick starts at ms = (last + 1) / factor. <= 0 when it already has. */
static inline double kz_frame_wait_left_ms(int32_t last, double factor, int64_t now_ms)
{
    if (!(factor > 0.0)) return 0.0;
    return (double)(last + 1) / factor - (double)now_ms;
}

/* How long to sleep now (host microseconds), given the game milliseconds left and
 * the rate at which game time runs against host time (game ms per host ms, 1
 * unless PS3_TIMEBASE_SCALE): 0 when the remainder is within the margin the
 * lifted loop spins; otherwise the remainder less the margin in host time, at
 * most step_us (then the timer is read again). */
static inline uint32_t kz_frame_wait_sleep_us(double left_ms, double margin_ms, uint32_t step_us, double rate)
{
    if (left_ms <= margin_ms) return 0;
    if (!(rate > 0.0)) rate = 1.0;
    const double us = (left_ms - margin_ms) * 1000.0 / rate;
    return us > (double)step_us ? step_us : (uint32_t)us;
}

/* The rate seen over one sleep (game ms advanced over host ms slept), folded into
 * the estimate the next sleeps use; kept within [0.25, 16]. */
static inline double kz_frame_wait_rate(double est, double game_ms, double host_ms)
{
    if (!(host_ms > 0.2)) return est;   /* too short to say */
    double r = game_ms / host_ms;
    if (r < 0.25) r = 0.25;
    if (r > 16.0) r = 16.0;
    return est > 0.0 ? 0.5 * est + 0.5 * r : r;
}
#endif
