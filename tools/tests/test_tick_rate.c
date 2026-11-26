/* The game clock's tick factor (port/kz1hd/tick_rate.h): trunc(ms x factor), as func_0021C0F8 counts ticks, gives
 * exactly ms x hz / 1000 whole ticks at every millisecond of a long session, at every rate the port uses; and the
 * plain hz / 1000.0 the port wrote before does not at 144 Hz (its boundaries at 375, 750, 875 ms came 1 ms late). */
#include <stdint.h>
#include <stdio.h>
#include "tick_rate.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

/* milliseconds in [from, from + n) at which trunc(ms x f) is not the exact tick count; the first in *first */
static long misses(int hz, double f, int64_t from, int64_t n, int64_t* first)
{
    long bad = 0;
    *first = -1;
    for (int64_t ms = from; ms < from + n; ms++) {
        const int64_t got = (int64_t)((double)ms * f), want = ms * hz / 1000;
        if (got != want) { if (*first < 0) *first = ms; bad++; }
    }
    return bad;
}

int main(void)
{
    static const int rates[] = {30, 60, 120, 144, 165, 240};
    char what[160];
    int64_t first;
    for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        const int hz = rates[i];
        const double f = kz_tick_factor(hz);
        long bad = misses(hz, f, 0, 10LL * 3600 * 1000, &first);   /* every ms of the first 10 hours */
        snprintf(what, sizeof what, "%d Hz: every ms of 10 hours gives the exact tick (%ld off, first at %lld)", hz, bad,
                 (long long)first);
        CHECK(bad == 0, what);
        bad = misses(hz, f, 30LL * 24 * 3600 * 1000, 10LL * 1000 * 1000, &first);   /* 10 000 s after 30 days */
        snprintf(what, sizeof what, "%d Hz: still exact after 30 days of uptime (%ld off)", hz, bad);
        CHECK(bad == 0, what);
    }
    long bad = misses(144, 144 / 1000.0, 0, 5000, &first);
    snprintf(what, sizeof what, "the old 144 / 1000.0: %ld late boundaries in 5000 ms, the first at %lld", bad,
             (long long)first);
    CHECK(bad == 15 && first == 375, what);
    CHECK(kz_tick_factor(30) > 0.03 && kz_tick_factor(30) < 0.0300000001, "30 Hz: just above 0.03, as the game's own");
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    puts("all passed");
    return 0;
}
