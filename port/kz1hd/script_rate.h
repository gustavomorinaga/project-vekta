/*
 * When the mission scripts' OnUpdate runs (native_ppu/script_update.cpp).
 * Header-only, plain C, so a host test can check it
 * (tools/tests/test_script_rate.c).
 *
 * The main loop (func_0021AB28, its object at 0x828734, the game clock's
 * fields inside it) calls the Lua function OnUpdate of the level's scripts
 * once per frame (the calls at 0x21AE18 and 0x21AE34, name "OnUpdate" at
 * 0x5436D4), whatever game time the frame advanced. On the PS3 a frame is at
 * least one 1/30 s tick, so OnUpdate ran 30 times a second, and the scripts
 * count in those calls: in 11.SDDockingBay the platform to the main station
 * moves 0.34 per call over 502.3, 1477 calls, about 49 s. With the port's
 * game clock at 60 or 144 ticks a second the frames come 2 to 5 times as
 * often, and so did OnUpdate: the ride took about 12 s, the story events
 * timed in seconds fell behind it and the section's end never came.
 *
 * Now OnUpdate runs once per 1/30 s of game time, at most once a frame, as
 * on the PS3: on a frame whose tick count (clock +0x6C) has entered a new
 * 1/30 s step. At 30 ticks a second every frame does, as before.
 *
 * The script message queue (object +0xB4: ready messages, count +0x8, array
 * +0x10) is emptied by the main loop right after OnUpdate, every frame
 * (0x21AEC0 to 0x21BB98): area triggers, finished dialogue and delayed
 * messages that arrive on a frame without OnUpdate would be lost, and with
 * them doors and enemy waves. So a frame with messages waiting calls OnUpdate
 * even inside a step; the call is taken from the following steps (a debt, at
 * most SCRIPT_RATE_DEBT_MAX calls, then forgiven), so the rate stays 30 a
 * second and no message is dropped.
 */
#ifndef KZ_SCRIPT_RATE_H
#define KZ_SCRIPT_RATE_H
#include <stdint.h>

#define SCRIPT_RATE_HZ 30        /* the game's own: one OnUpdate per 1/30 s tick */
#define SCRIPT_RATE_DEBT_MAX 8   /* calls ahead of the 30 a second (about 0.27 s) */

/* The game clock's ticks per second from its seconds per tick (clock +0x54,
 * 1/30 the game's, 1/hz with KZ_TICK_HZ); 30 for anything unreadable. */
static inline int script_rate_clock_hz(float seconds_per_tick)
{
    if (!(seconds_per_tick > 0.0f) || seconds_per_tick > 1.0f) return SCRIPT_RATE_HZ;
    const int hz = (int)(1.0f / seconds_per_tick + 0.5f);
    return hz > 0 ? hz : SCRIPT_RATE_HZ;
}

/* One call site's state; zero it to start ({0}). */
struct script_rate {
    int started;
    int64_t base;   /* the 1/30 s step the count starts from */
    int64_t last;   /* the step at the last frame */
    int64_t calls;  /* calls since base */
};

/* Whether OnUpdate runs this frame. `ticks` is the clock's tick count after
 * this frame's step, `hz` the clock's rate, `pending` whether the script
 * message queue holds messages (they are dropped after this frame). */
static inline int script_rate_due(struct script_rate* s, uint32_t ticks, int hz, int pending)
{
    if (hz <= SCRIPT_RATE_HZ) { s->started = 0; return 1; }   /* every frame is a whole 1/30 s or more */
    const int64_t step = (int64_t)ticks * SCRIPT_RATE_HZ / hz;
    if (!s->started || step < s->last) {   /* the first frame, or a new clock */
        s->started = 1; s->base = step; s->calls = 0;
    }
    s->last = step;
    int64_t allowed = step - s->base + 1;   /* calls 30 a second allows by now */
    /* no catching up: a frame that spans several steps makes one call, as on the PS3 */
    if (allowed - s->calls > 1) { s->base += allowed - s->calls - 1; allowed = s->calls + 1; }
    if (s->calls < allowed) { s->calls++; return 1; }
    if (!pending) return 0;
    /* messages waiting: call now, ahead of the rate; past the cap the debt is forgiven */
    s->calls++;
    if (s->calls - allowed > SCRIPT_RATE_DEBT_MAX) s->base -= s->calls - allowed - SCRIPT_RATE_DEBT_MAX;
    return 1;
}
#endif
