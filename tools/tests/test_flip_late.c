/* flip_late.h (KZ_FLIP_LATE): at the 60 fps limit a flip that missed its tick is shown at once and the ticks go on
 * from there. A simulated game, as in test_flip_phase.c, starts each frame once its flip is done and its own 60 Hz
 * clock (whole milliseconds, as the game's timer) has ticked, and builds it in a time set per frame; the frame clock
 * shows flips on the 59.94 Hz cadence's ticks as main.cpp does, its passes 100 us apart. Checked, against the cadence
 * as before (not armed): when for the modes it arms; frames that fit are shown at exactly the same times as before,
 * with no flip shown at once; frames that do not fit are shown when done, never closer than a tick apart and never a
 * whole tick late; a single slow frame costs its own time, not two ticks; and a game whose frames are done before
 * each tick (menus) is not changed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flip_late.h"

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); fails++; } \
                            else { printf("ok   " __VA_ARGS__); putchar('\n'); } } while (0)

#define PERIOD (1e6 / 59.94)
#define PASS 100.0
#define MAXP 8192

/* a frame's build time, us: from its index alone, so two runs build the same frames */
enum { W_FIT, W_FIT16, W_HEAVY, W_HITCH, W_SLOW20, W_SLOW40 };
static double u01(unsigned i) { unsigned h = i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return (h % 100000) / 1e5; }
static double work(int kind, unsigned i)
{
    switch (kind) {
    case W_FIT:    return 4300 + (u01(i) - 0.5) * 600;
    case W_FIT16:  return 4000 + u01(i) * 11500;          /* 4 to 15.5 ms: fits, the flip waits for its tick */
    case W_HEAVY:  return 14000 + u01(i) * 5000;          /* 14 to 19 ms: the 2-player match */
    case W_HITCH:  return i == 300 ? 20000 : 4300;        /* one slow frame */
    case W_SLOW20: return 20000;
    default:       return 40000;
    }
}

struct result { int n; double t[MAXP], reach[MAXP]; unsigned long long late, late_settled; };

/* armed: the late rule on; queued: the game's next frame is always done before the tick (reached in its own drain) */
static void run(int armed, int kind, int queued, double secs, struct result* r)
{
    struct kz_flip_late s; memset(&s, 0, sizeof s);
    memset(r, 0, sizeof *r);
    double next = 0, reach_at = 0;
    int pending = 0, flipped = 0, building = 0, done = 1, game_tick = -1;
    unsigned frame = 0;
    for (double t = 0; t < secs * 1e6; t += 10) {
        const int pass = fmod(t, PASS) < 10;   /* the frame clock runs one pass every 100 us */
        if (pass && t >= next) {               /* a tick: show the flip held, then drain */
            flipped = 0;
            int presented = 0;
            if (pending) { pending = 0; presented = 1; done = 1; if (r->n < MAXP) r->t[r->n++] = t;
                           if (r->n == 10) r->late_settled = s.late; }
            kz_flip_late_tick(&s, presented);
            next += PERIOD;
            if (queued && done) { pending = 1; flipped = 1; done = 0; if (r->n < MAXP) r->reach[r->n] = t; }   /* waiting in the FIFO */
        }
        if (!queued) {
            /* the game: starts a frame when its last flip is done and its clock ticked since the last start */
            const int now_tick = (int)((int)(t / 1000) * 0.06 * (1.0 + 1e-12));
            if (!building && done && now_tick > game_tick) {
                building = 1; done = 0; game_tick = now_tick; reach_at = t + work(kind, frame++);
            }
            if (pass && building && !flipped && t >= reach_at) {   /* the drain reaches it */
                building = 0; pending = 1; flipped = 1; if (r->n < MAXP) r->reach[r->n] = t;
            }
        }
        if (pass && flipped && pending && kz_flip_late_reach(&s, armed)) next = t + PASS;   /* main.cpp: next_tick = now, fired on the next pass */
    }
    r->late = s.late;
    r->late_settled = s.late - r->late_settled;   /* after the first 10 presents (the first flips follow ticks that showed nothing) */
}

struct stats { double fps, iv_min, iv_max, wait_max; int doubled, missed, missed_slow; };
/* missed: frames reached after a tick that showed nothing; missed_slow: of those, shown more than 2 passes after */
static struct stats measure(const struct result* r, int from)
{
    struct stats st = { 0, 1e30, 0, 0, 0, 0, 0 };
    for (int i = from + 1; i < r->n; i++) {
        const double iv = r->t[i] - r->t[i - 1], wait = r->t[i] - r->reach[i];
        if (iv < st.iv_min) st.iv_min = iv;
        if (iv > st.iv_max) st.iv_max = iv;
        if (wait > st.wait_max) st.wait_max = wait;
        if (iv > 1.5 * PERIOD) st.doubled++;
        /* the last tick before its reach showed nothing: a tick fell between the last present and the reach */
        if (r->reach[i] - r->t[i - 1] > PERIOD + PASS) { st.missed++; if (wait > 2 * PASS + 10) st.missed_slow++; }
    }
    st.fps = r->n - from > 1 ? (r->n - 1 - from) * 1e6 / (r->t[r->n - 1] - r->t[from]) : 0;
    return st;
}
/* frames that fit: after the first 10 presents, every interval a tick (to a pass) and the same count */
static int on_ticks(const struct result* r)
{
    for (int i = 11; i < r->n; i++) if (fabs(r->t[i] - r->t[i - 1] - PERIOD) > PASS + 10) return 0;
    return 1;
}

static struct result A, B;

int main(void)
{
    /* when it arms */
    EXPECT(kz_flip_late_armed(1, 0, 1, 60, 59.94), "armed at the 60 limit (60 Hz game clock, 59.94 cadence, own vblanks)");
    EXPECT(!kz_flip_late_armed(0, 0, 1, 60, 59.94), "KZ_FLIP_LATE=0: not armed");
    EXPECT(!kz_flip_late_armed(1, 0, 1, 30, 59.94), "the 30 limit (the game's own pacing): not armed");
    EXPECT(!kz_flip_late_armed(1, 0, 1, 144, 144.0) && !kz_flip_late_armed(1, 0, 1, 144, 1000.0) &&
           !kz_flip_late_armed(1, 0, 1, 60, 1000.0), "from 100 up, or a cadence from 100 up: not armed");
    EXPECT(!kz_flip_late_armed(1, 1, 1, 60, 60.0), "the phase lock on: not armed");
    EXPECT(!kz_flip_late_armed(1, 0, 0, 60, 59.94), "vblanks ticked with the flips (no cadence of their own): not armed");

    /* frames that fit: the same present times as before, to the microsecond */
    run(0, W_FIT, 0, 20, &A); run(1, W_FIT, 0, 20, &B);
    struct stats a = measure(&A, 10), b = measure(&B, 10);
    EXPECT(on_ticks(&A) && on_ticks(&B) && abs(A.n - B.n) <= 1 && B.late_settled == 0,
           "4.3 ms frames: %d and %d presents, all a tick apart; %llu shown at once once settled (%llu at the start)",
           A.n, B.n, B.late_settled, B.late);
    EXPECT(fabs(a.fps - 59.94) < 0.05 && fabs(b.fps - 59.94) < 0.05, "  %.2f and %.2f fps", a.fps, b.fps);
    run(0, W_FIT16, 0, 20, &A); run(1, W_FIT16, 0, 20, &B);
    /* the first flips, shown at once after ticks that showed nothing, start the ticks a few ms after the game's own
     * clock; until its ticks drift past them (12 s at most) a long frame can miss its tick by a hair, and is then
     * shown at once: a tick and a pass after the last, as on the ticks */
    EXPECT(on_ticks(&A) && on_ticks(&B) && abs(A.n - B.n) <= 1 && B.late_settled <= 2,
           "4 to 15.5 ms frames: %d and %d presents, all a tick apart, %llu shown at once", A.n, B.n, B.late_settled);
    run(0, W_FIT, 1, 20, &A); run(1, W_FIT, 1, 20, &B);
    EXPECT(on_ticks(&A) && on_ticks(&B) && abs(A.n - B.n) <= 1 && B.late_settled == 0,
           "frames done before each tick (menus): %d and %d presents, all a tick apart, %llu at once", A.n, B.n, B.late_settled);

    /* frames that do not fit (14 to 19 ms): before, a mix of one and two ticks */
    run(0, W_HEAVY, 0, 60, &A); run(1, W_HEAVY, 0, 60, &B);
    a = measure(&A, 10); b = measure(&B, 10);
    EXPECT(a.fps < 50 && a.doubled > (A.n - 11) / 5, "14 to 19 ms frames before: %.1f fps, %d of %d shown two ticks after the last",
           a.fps, a.doubled, A.n - 11);
    EXPECT(b.fps > 56 && b.doubled == 0, "  with the rule: %.1f fps, %d shown two ticks after the last", b.fps, b.doubled);
    EXPECT(b.iv_min > PERIOD - PASS - 10 && b.missed > 0 && b.missed_slow == 0 && a.missed_slow > a.missed / 2,
           "  never closer than a tick (%.2f ms); %d frames missed a tick, all shown within 2 passes (before: %d of %d later)",
           b.iv_min / 1000, b.missed, a.missed_slow, a.missed);
    EXPECT(b.wait_max < PERIOD, "  no flip waits a whole tick (longest %.2f ms)", b.wait_max / 1000);
    EXPECT(B.late > (unsigned long long)B.n / 4, "  %llu of %d shown at once", B.late, B.n);

    /* one slow frame among fast ones */
    run(0, W_HITCH, 0, 10, &A); run(1, W_HITCH, 0, 10, &B);
    a = measure(&A, 10); b = measure(&B, 10);
    EXPECT(a.iv_max > 2 * PERIOD - 2 * PASS, "one 20 ms frame before: shown %.2f ms after the last", a.iv_max / 1000);
    EXPECT(b.iv_max < a.iv_max - 5000 && b.iv_min > PERIOD - PASS - 10 && B.late_settled == 1 && b.missed == 1 && b.missed_slow == 0,
           "  with the rule: %.2f ms (shown when done), the next a tick or more after it (%.2f), %llu shown at once",
           b.iv_max / 1000, b.iv_min / 1000, B.late_settled);
    {   /* and the frames after it are shown on ticks again: a tick apart */
        int on = 1;
        for (int i = 320; i + 1 < B.n; i++) if (fabs(B.t[i + 1] - B.t[i] - PERIOD) > PASS + 10) on = 0;
        EXPECT(on, "  the frames after it a whole tick apart again");
    }

    /* slow scenes: 20 ms and 40 ms a frame */
    run(0, W_SLOW20, 0, 20, &A); run(1, W_SLOW20, 0, 20, &B);
    a = measure(&A, 10); b = measure(&B, 10);
    EXPECT(fabs(a.fps - 29.97) < 0.2 && b.fps > 49 && b.fps < 50.1, "20 ms frames: %.2f fps before, %.2f with the rule", a.fps, b.fps);
    run(0, W_SLOW40, 0, 20, &A); run(1, W_SLOW40, 0, 20, &B);
    a = measure(&A, 10); b = measure(&B, 10);
    EXPECT(fabs(a.fps - 19.98) < 0.2 && b.fps > 24.6 && b.fps < 25.1, "40 ms frames: %.2f fps before, %.2f with the rule", a.fps, b.fps);

    printf("test_flip_late: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
