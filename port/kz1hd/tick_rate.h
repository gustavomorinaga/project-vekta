/*
 * The game clock's ticks per millisecond for a tick rate (main.cpp, kz_tick_rate).
 * Header-only, plain C, so a host test can check it (tools/tests/test_tick_rate.c).
 *
 * func_0021C0F8 counts ticks as trunc(ms x factor), ms being the game's whole-millisecond timer (func_0001CC20)
 * and factor the double at 0x21C0F0. The game's own 30 Hz factor is 0.03000000003 (0x3F9EB851EC090FB8), a hair
 * above 0.03: 0.03 itself is stored just below, and the product at an exact boundary (ms = 100, 200, ...) would
 * then truncate one tick short. Written as hz / 1000.0, 144 Hz has that problem (0.144 is stored just below):
 * 15 boundaries in every 5000 ms (ms = 375, 750, 875, ...) came 1 ms late, a frame of 8 ms between two of 6 or 7.
 * The factor is therefore raised by one part in 10^12: enough to clear the double's rounding (about one part in
 * 10^16), too little to move a boundary early before the timer passes 5 x 10^10 ms.
 */
#ifndef KZ_TICK_RATE_H
#define KZ_TICK_RATE_H

static inline double kz_tick_factor(int hz)
{
    return hz / 1000.0 * (1.0 + 1e-12);
}

#endif
