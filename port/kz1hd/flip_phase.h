/*
 * KZ_FLIP_PHASE_US (main.cpp, experimental, off by default): when the frame clock presents a flip at the 60 fps
 * limit. Header-only, plain C, so a host test can check it (tools/tests/test_flip_phase.c).
 *
 * A flip the FIFO drain reaches is shown on the next tick of the flip cadence (59.94 below 100 fps, #166), and the
 * game starts its next frame when that flip is done. So in Level 1 at the 60 limit each frame was built in about 4.3
 * ms after a tick and then waited 12.2 to 12.5 ms for the next one, and a turn of the mouse reached the screen 40 to
 * 43 ms after the game took the counts (measured 2026-11-26, V-Sync off and on, 3,476 flips each; KZ_VBLANK_HZ=60
 * and 59.9988 the same, 12.4 ms: the game follows the flips, not its clock). Presenting each flip at once
 * (KZ_VBLANK_HZ=1000) gave 25.4 ms, but frames 14.1 to 19.0 ms apart (p99 19.0, 1 % low 51.4): with the flips
 * done at once the game starts on its own 60 Hz clock, whole milliseconds plus its wake-up, and arrives 14.6 to 18.7
 * ms apart (p1, p99).
 *
 * With the lock the cadence is the game clock's own rate (60 Hz, a tick every 16666.67 us) and its ticks move to
 * `margin` after the flips usually arrive. Every `KZ_FLIP_PHASE_WINDOW` flips the latest of them sets how far the
 * ticks may move earlier: half the spare time, at most `KZ_FLIP_PHASE_STEP` (1 ms) at a time, or
 * `KZ_FLIP_PHASE_JUMP` (4 ms) when more than 6 ms are spare. They never move later for a flip that came close: that
 * drift put them after the game clock's ticks within seconds, where the game waits for its flip again, every flip
 * arrives 12 ms before its tick, and 1 ms steps took seconds to get out. A flip that missed its tick (the last tick
 * presented nothing and the flip came within half a period of it) is presented `margin` from now by a tick of its own
 * and the ticks keep their phase; two in a row move the ticks to `margin` after the second. A flip reached by a
 * tick's own drain (the game waiting with its next frame done: menus, loads) does not move them.
 *
 * Measured with margin 2500, Level 1 walk + 12 yaw bursts per run (2026-11-26): mean latency 29.6 to 41.8 ms (36.6,
 * 41.8, 29.6, 35.2 in four runs; 31.6 for an earlier version), frames p99 17.2 to 17.6 ms, 1 % low 49.7 to 55.9 fps,
 * against 38.9 to 42.5 ms, p99 16.8 and 1 % low 58.6 to 59.0 without it. The longer frames come from the ticks themselves
 * firing up to 1.5 ms late on a busy host, and from flips that miss. Hence off by default.
 */
#ifndef KZ_FLIP_PHASE_H
#define KZ_FLIP_PHASE_H

#define KZ_FLIP_PHASE_WINDOW 32
#define KZ_FLIP_PHASE_STEP 1000.0   /* us: the most the tick moves earlier in one window */
#define KZ_FLIP_PHASE_JUMP 4000.0   /* us: the same, with more than 6 ms spare */

struct kz_flip_phase {
    double period, margin;   /* us */
    double next, last;       /* the next tick (the grid's, or a late flip's own) and the last one, us */
    double grid;             /* the next tick of the grid, us */
    int last_idle;           /* the last tick presented nothing */
    int late_run;            /* late flips in a row */
    double win_min;          /* the least spare time in this window, us */
    int win_n;
    unsigned late, backoff, earlier;   /* counts: late flips, close ones, moves earlier */
};

static inline void kz_flip_phase_start(struct kz_flip_phase* p, double hz, double margin_us, double now)
{
    p->period = 1e6 / hz; p->margin = margin_us;
    p->next = p->grid = now; p->last = now - p->period; p->last_idle = 0; p->late_run = 0;
    p->win_min = 1e30; p->win_n = 0;
    p->late = p->backoff = p->earlier = 0;
}

/* A tick fired at p->next; `presented`: it showed a flip. */
static inline void kz_flip_phase_tick(struct kz_flip_phase* p, int presented)
{
    if (p->next < p->grid) { p->next = p->grid; return; }   /* a late flip's own tick */
    p->last = p->grid; p->grid += p->period; p->next = p->grid; p->last_idle = !presented;
}

/* The drain reached a flip at `now`; `in_tick`: in the drain of a tick itself. */
static inline void kz_flip_phase_reach(struct kz_flip_phase* p, double now, int in_tick)
{
    if (in_tick) return;
    if (p->last_idle && now - p->last < p->period / 2) {   /* missed its tick */
        p->late++;
        if (++p->late_run >= 2) { p->grid = p->next = now + p->margin; p->win_min = 1e30; p->win_n = 0; }
        else if (now + p->margin < p->grid) p->next = now + p->margin;
        return;
    }
    p->late_run = 0;
    const double s = p->grid - now;
    if (s < p->margin / 2) p->backoff++;   /* close to missing: counted only */
    if (s < p->win_min) p->win_min = s;
    if (++p->win_n < KZ_FLIP_PHASE_WINDOW) return;
    const double spare = p->win_min - p->margin;
    if (spare > 250.0) {
        const double cap = spare > 6000.0 ? KZ_FLIP_PHASE_JUMP : KZ_FLIP_PHASE_STEP;
        p->grid -= spare / 2 < cap ? spare / 2 : cap;
        p->next = p->grid;
        p->earlier++;
    }
    p->win_min = 1e30; p->win_n = 0;
}

#endif
