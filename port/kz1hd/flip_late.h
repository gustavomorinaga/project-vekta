/*
 * KZ_FLIP_LATE (main.cpp, default 1; 0 = as before): at the 60 fps limit, a flip that missed its tick is shown at
 * once, and the next tick comes a whole tick after it. Header-only, plain C, so a host test can check it
 * (tools/tests/test_flip_late.c).
 *
 * At the 60 limit a flip is shown on the next tick of the 59.94 Hz flip cadence, and the game starts its next frame
 * when that flip is done (flip_phase.h). A frame built within a tick waits for the next tick: frames are shown a
 * whole tick apart, evenly, as before. But a frame that took a little more than a tick arrived just after a tick that
 * showed nothing, and waited for the next one: shown 33.4 ms after the last instead of about 17, and the game started
 * its next frame that much later. A scene that needs 15 to 19 ms a frame then ran at a mix of 60 and 30 fps:
 * 2-player split screen at 3440x1440 on Windows, 46 to 48 fps, frames p50 16.7 ms, p99 34 to 43 ms (2026-11-28),
 * where the 144 limit, whose flips are shown at once in play, ran the same match at 60 to 65 fps.
 *
 * Here such a flip (the last tick showed nothing) is shown when the drain reaches it, by a tick fired at once, and
 * the cadence goes on from that tick. So no two frames are shown closer than a tick apart, a frame that fits is shown
 * on its tick as before, and a frame that does not is shown when it is done instead of up to a tick later. Left as
 * they were: the 30 limit (the game's own pacing: every second tick shows nothing there), from 100 up (flips at once
 * in play already, menu_mouse), the guest's vblanks (their own cadence; armed only when they have one), and the
 * phase lock (KZ_FLIP_PHASE_US), which handles late flips itself.
 */
#ifndef KZ_FLIP_LATE_H
#define KZ_FLIP_LATE_H

struct kz_flip_late {
    int idle;                  /* the last tick showed nothing */
    unsigned long long late;   /* flips shown at once */
};

/* mode: KZ_FLIP_LATE; game_hz: the game clock (g_kz_tick_hz); cadence_hz: the flip cadence's ticks. */
static inline int kz_flip_late_armed(int mode, int phase_on, int own_vblank, int game_hz, double cadence_hz)
{
    return mode && !phase_on && own_vblank && game_hz > 30 && game_hz < 100 && cadence_hz > 0 && cadence_hz < 100.0;
}

/* A tick fired; `presented`: it showed a flip. */
static inline void kz_flip_late_tick(struct kz_flip_late* s, int presented) { s->idle = !presented; }

/* The drain holds a flip it reached since the last tick. Returns 1 when that flip is to be shown now (by a tick fired
 * at once): it is armed and the last tick showed nothing. Once per flip: the tick that shows it sets `idle` again. */
static inline int kz_flip_late_reach(struct kz_flip_late* s, int armed)
{
    if (!armed || !s->idle) return 0;
    s->idle = 0;
    s->late++;
    return 1;
}

#endif
