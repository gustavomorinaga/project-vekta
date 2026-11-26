/* flip_phase.h (KZ_FLIP_PHASE_US): at the 60 fps limit the flip cadence's ticks move to just after the flips
 * arrive. A simulated game builds each frame in 4.3 ms (+-0.3) once its flip is done and its 60 Hz clock (whole milliseconds, as the game's
 * timer) has ticked; the frame clock presents on its ticks, as main.cpp does. Checked: the wait from a flip's arrival
 * to its present falls from about 12 ms to the margin, the intervals stay a whole tick, no flip misses its tick once
 * settled, a late flip is shown at once, a game that only waits for its flips is not sped up, and a slow game is not
 * chased. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "flip_phase.h"

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); fails++; } \
                            else { printf("ok   " __VA_ARGS__); putchar('\n'); } } while (0)

struct result { double wait_mean, iv_min, iv_max; int misses, presents; unsigned late_half; };

static unsigned rng = 12345;
static double jitter(double a) { rng = rng * 1103515245u + 12345u; return ((rng >> 8) % 2001 / 1000.0 - 1.0) * a; }

/* lock: use flip_phase.h (else ticks every 1e6/hz); work: the frame's build time, us; clock: the game waits for its
 * own 60 Hz clock too; queued: the game's next frame is always done before the tick (it reaches in the tick's own
 * drain). Steps of 10 us for `secs`; statistics over the last half. */
static struct result run(int lock, double hz, double work, int clock, int queued, double secs, struct kz_flip_phase* out)
{
    struct kz_flip_phase p;
    kz_flip_phase_start(&p, hz, 2500.0, 0.0);
    const double period = 1e6 / hz;
    double next = 0, frame_start = 0, reach_at = work, last_present = -1;
    int pending = 0, building = !queued, done = queued, game_tick = 0;
    unsigned late0 = 0; int halved = 0;
    double wsum = 0; int wn = 0, misses = 0, presents = 0;
    double iv_min = 1e30, iv_max = 0, reached = -1;
    struct result r;
    for (double t = 0; t < secs * 1e6; t += 10) {
        const int half = t > secs * 1e6 / 2;
        if (half && !halved) { halved = 1; late0 = p.late; }
        if (t >= next) {   /* a tick: present the flip the drain reached, then drain (a done frame is reached here) */
            int presented = 0;
            if (pending) {
                pending = 0; presented = 1; presents++;
                if (half) { wsum += t - reached; wn++;
                            if (last_present >= 0) {
                                const double iv = t - last_present;
                                if (iv < iv_min) iv_min = iv;
                                if (iv > iv_max) iv_max = iv;
                            } }
                last_present = t; done = 1;
            } else if (half && presents) misses++;
            if (lock) { kz_flip_phase_tick(&p, presented); next = p.next; } else next += period;
            if (queued && done) { pending = 1; reached = t; done = 0; if (lock) { kz_flip_phase_reach(&p, t, 1); next = p.next; } }
        }
        if (queued) continue;
        /* the game: starts a frame when its last flip is done and (clock) its clock ticked since the last start */
        const int ms = (int)(t / 1000);
        const int now_tick = (int)(ms * 0.06 * (1.0 + 1e-12));
        if (!building && done && (!clock || now_tick > game_tick)) {
            building = 1; done = 0; frame_start = t; game_tick = now_tick; reach_at = t + work + jitter(300);
        }
        if (building && t >= reach_at) {   /* the drain reaches its flip between ticks */
            building = 0; pending = 1; reached = t;
            if (lock) { kz_flip_phase_reach(&p, t, 0); next = p.next; }
        }
    }
    (void)frame_start;
    r.wait_mean = wn ? wsum / wn : 0; r.iv_min = iv_min; r.iv_max = iv_max; r.misses = misses; r.presents = presents; r.late_half = p.late - late0;
    if (out) *out = p;
    return r;
}

int main(void)
{
    struct kz_flip_phase p;
    const struct result old = run(0, 59.94, 4300, 1, 0, 60, NULL);
    EXPECT(old.wait_mean > 11000, "59.94 cadence (0.8.0 before): a flip waits %.1f ms for its tick", old.wait_mean / 1000);
    const struct result r = run(1, 60.0, 4300, 1, 0, 60, &p);
    EXPECT(r.wait_mean < 3500 && r.wait_mean > 1500, "60 Hz with the phase lock: a flip waits %.2f ms (margin 2.5)",
           r.wait_mean / 1000);
    EXPECT(r.misses == 0 && r.late_half == 0, "settled: no tick without a flip (%d), no late flip (%u)", r.misses, r.late_half);
    EXPECT(r.iv_min > 15600 && r.iv_max < 17700, "settled: intervals %.2f to %.2f ms", r.iv_min / 1000, r.iv_max / 1000);
    EXPECT(fabs(r.presents / 60.0 - 60.0) < 0.5, "%d presents in 60 s", r.presents);

    /* a flip that misses its tick is presented `margin` later, not a whole tick later */
    kz_flip_phase_start(&p, 60.0, 2500.0, 0.0);
    kz_flip_phase_tick(&p, 0);   /* the tick at 0 presented nothing */
    kz_flip_phase_reach(&p, 400.0, 0);
    EXPECT(p.late == 1 && fabs(p.next - 2900.0) < 1e-6, "a late flip: shown at %.1f ms, 0.4 ms after it arrived + 2.5", p.next / 1000);
    kz_flip_phase_tick(&p, 1);
    EXPECT(fabs(p.next - 1e6 / 60.0) < 1e-6, "after a late flip's own tick the ticks keep their phase (next at %.2f ms)", p.next / 1000);
    kz_flip_phase_tick(&p, 1);
    EXPECT(fabs(p.next - 2e6 / 60.0) < 1e-6 && !p.last_idle, "and go on a whole period apart (%.2f ms)", p.next / 1000);
    /* a flip close to its tick is counted, and the tick stays */
    kz_flip_phase_start(&p, 60.0, 2500.0, 0.0);
    kz_flip_phase_tick(&p, 1);   /* next tick at 16666.7 */
    kz_flip_phase_reach(&p, 16000.0, 0);
    EXPECT(p.backoff == 1 && fabs(p.next - 1e6 / 60.0) < 1e-6, "a flip 0.67 ms before its tick: the tick stays at %.2f ms",
           p.next / 1000);

    /* a game that only waits for its flips (menus: its next frame done before each tick) keeps the period */
    const struct result q = run(1, 60.0, 4300, 0, 1, 20, &p);
    EXPECT(fabs(q.presents / 20.0 - 60.0) < 0.2 && p.earlier == 0, "flips reached in the tick's drain: %.2f fps, the tick never moved earlier",
           q.presents / 20.0);
    /* a game that waits only for its flips and builds between ticks (Killzone HD's frames wait for a clock tick too):
     * the tick creeps earlier by at most a step a window */
    const struct result f = run(1, 60.0, 4300, 0, 0, 60, &p);
    EXPECT(f.presents / 60.0 < 60.5, "a game paced only by its flips: %.2f fps, less than 1 %% fast", f.presents / 60.0);
    /* a slow game (20 ms a frame): more frames shown than on every second tick, none held past two ticks */
    const struct result s = run(1, 60.0, 20000, 1, 0, 60, &p);
    EXPECT(s.presents / 60.0 > 35 && s.presents / 60.0 < 46 && s.iv_max < 2e6 / 60.0,
           "20 ms frames: %.1f fps shown, intervals %.2f to %.2f ms", s.presents / 60.0, s.iv_min / 1000, s.iv_max / 1000);

    printf("test_flip_phase: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
