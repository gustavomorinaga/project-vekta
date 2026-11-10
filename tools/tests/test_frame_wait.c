/* The frame clock's wait without spinning (port/kz1hd/native_ppu/frame_wait.h), against a fake clock: the sleeps never
 * reach the next tick (the lifted loop still takes it exactly) with the 2 ms margin the port uses (the game's timer
 * counts whole milliseconds), the remainder left to spin is at most the margin plus one wake-up, and this holds at the 30, 60 and 144 Hz factors and when guest time runs faster than host time. */
#include <stdio.h>
#include "frame_wait.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

/* Simulates one wait: guest ms = host us * scale / 1000; returns the guest ms left when the sleeps stop (what the
 * lifted loop spins), and sets *overshot when a sleep went past the tick. */
static double s_rate = 1.0;   /* the estimate, kept from one wait to the next as the port keeps it */
static double simulate(double factor, int32_t last, double now_ms, double scale, double margin, int* overshot, int* sleeps)
{
    double t = now_ms;
    *overshot = 0; *sleeps = 0;
    for (int i = 0; i < 1000; i++) {
        const double left = kz_frame_wait_left_ms(last, factor, (int64_t)t);
        const unsigned us = kz_frame_wait_sleep_us(left, margin, 1000, s_rate);
        if (!us) return kz_frame_wait_left_ms(last, factor, (int64_t)t);
        const double host_ms = us / 1000.0 + 0.05;   /* the sleep, plus 50 us of wake-up lateness */
        const double t0 = t;
        t += host_ms * scale;
        s_rate = kz_frame_wait_rate(s_rate, (double)((int64_t)t - (int64_t)t0), host_ms);
        (*sleeps)++;
        if ((int)(t * factor) > last) { *overshot = 1; return 0; }
    }
    return -1;
}

int main(void)
{
    CHECK(kz_frame_wait_left_ms(59, 60.0 / 1000.0, 990) > 9.9 && kz_frame_wait_left_ms(59, 60.0 / 1000.0, 990) < 10.1,
          "60 Hz: tick 59 seen at 990 ms, the next starts at 1000 ms: 10 ms left");
    CHECK(kz_frame_wait_left_ms(60, 60.0 / 1000.0, 1017) <= 0, "already past the next tick: nothing left");
    CHECK(kz_frame_wait_left_ms(5, 0.0, 10) == 0.0, "a factor of 0 (not set): no wait");
    CHECK(kz_frame_wait_sleep_us(0.8, 1.0, 1000, 1.0) == 0, "within the margin: no sleep");
    CHECK(kz_frame_wait_sleep_us(1.5, 1.0, 1000, 1.0) == 500, "1.5 ms left, margin 1: sleep 0.5 ms");
    CHECK(kz_frame_wait_sleep_us(9.0, 1.0, 1000, 1.0) == 1000, "9 ms left: one step of at most 1 ms");
    CHECK(kz_frame_wait_sleep_us(1.5, 1.0, 1000, 3.0) == 166, "game time at 3x: the same 0.5 game ms is 0.17 host ms");
    const double hz[3] = {30.0, 60.0, 144.0};
    int bad = 0, maxleft_bad = 0;
    for (int h = 0; h < 3; h++)
        for (double scale = 1.0; scale <= 3.0; scale += 1.0) {
            s_rate = 1.0;
            int first = 1;
            for (int start = 0; start < 2000; start += 7) {
                const double f = hz[h] / 1000.0;
                const int32_t last = (int32_t)(start * f);
                int over, sl;
                const double left = simulate(f, last, start, scale, 2.0, &over, &sl);
                if (over && !first) bad++;    /* the first wait at a new rate may not know it yet */
                if (!over && left > 2.0 + 1.05 * scale + 1e-9) maxleft_bad++;
                if (sl) first = 0;
            } }
    CHECK(bad == 0, "30/60/144 Hz, guest time at 1x to 3x host: after the first wait, no sleep reaches the next tick");
    CHECK(maxleft_bad == 0, "what is left to spin is at most the margin plus one step's lateness");
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    return 0;
}
