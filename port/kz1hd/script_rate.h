/*
 * When the mission scripts' OnUpdate runs (native_ppu/script_update.cpp).
 * Header-only, plain C, so a host test can check it
 * (tools/tests/test_script_rate.c).
 *
 * The main loop (func_0021AB28) calls the Lua function OnUpdate of the
 * level's scripts once per frame (the calls at 0x21AE18 and 0x21AE34, name
 * "OnUpdate" at 0x5436D4), whatever game time the frame advanced. On the
 * PS3 a frame is at least one 1/30 s tick, so OnUpdate ran 30 times a
 * second, and the scripts count in those calls: in 11.SDDockingBay the
 * platform to the main station moves 0.34 per call over 502.3, 1477 calls,
 * about 49 s. With the port's game clock at 60 or 144 ticks a second the
 * frames come 2 to 5 times as often, and so did OnUpdate: the ride took
 * about 12 s, the story events timed in seconds fell behind it and the
 * section's end never came.
 *
 * Now OnUpdate runs once per 1/30 s of game time, at most once a frame, as
 * on the PS3: on a frame whose tick count (clock +0x6C) has entered a new
 * 1/30 s step. At 30 ticks a second every frame does, as before.
 */
#ifndef KZ_SCRIPT_RATE_H
#define KZ_SCRIPT_RATE_H
#include <stdint.h>

#define SCRIPT_RATE_HZ 30   /* the game's own: one OnUpdate per 1/30 s tick */

/* The game clock's ticks per second from its seconds per tick (clock +0x54,
 * 1/30 the game's, 1/hz with KZ_TICK_HZ); 30 for anything unreadable. */
static inline int script_rate_clock_hz(float seconds_per_tick)
{
    if (!(seconds_per_tick > 0.0f) || seconds_per_tick > 1.0f) return SCRIPT_RATE_HZ;
    const int hz = (int)(1.0f / seconds_per_tick + 0.5f);
    return hz > 0 ? hz : SCRIPT_RATE_HZ;
}

/* Whether OnUpdate runs this frame. `ticks` is the clock's tick count after
 * this frame's step, `hz` the clock's rate, `last` the 1/30 s step of the
 * last call (one per call site; start it at SCRIPT_RATE_NONE). */
#define SCRIPT_RATE_NONE INT64_MIN
static inline int script_rate_due(uint32_t ticks, int hz, int64_t* last)
{
    if (hz <= SCRIPT_RATE_HZ) { *last = SCRIPT_RATE_NONE; return 1; }   /* every frame is a whole 1/30 s or more */
    const int64_t step = (int64_t)ticks * SCRIPT_RATE_HZ / hz;
    if (step == *last) return 0;
    *last = step;   /* a new step, or the count went back (a new clock) */
    return 1;
}
#endif
