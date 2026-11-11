/*
 * The writer of route files (port/kz1hd/route.cpp): the lines a recording
 * (KZ_ROUTE_REC) writes, and the ones a replay writes with
 * KZ_ROUTE_REBASE=<file> (a new route of what the replay gave the game and
 * found, so that later replays of this build can be checked against it).
 * Plain C so that a host test can build it on its own
 * (tools/tests/test_route_rebase.c).
 *
 * Why a rebase: a recording made by playing does not replay exactly. The game
 * reacts to resources that finish loading on other threads (in Level 7 a
 * character's facial animation, func_0022F6E0, then the faces' blink timer
 * func_0022B220 draws from the random generator a few frames earlier or
 * later), and that timing differs between the live recording and a replay,
 * while replays agree with each other (docs/dev/perf-bench.md). A replay of
 * the recorded input, written out as a route, is a route that later replays
 * on the same build and settings follow to the end.
 */
#ifndef KZ_ROUTE_REBASE_H
#define KZ_ROUTE_REBASE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KZ_ROUTE_PAD_BYTES 0x24

struct kz_route_writer {
    FILE* f;
    uint8_t last_pad[KZ_ROUTE_PAD_BYTES];
    int have_last;
};

/* the header; what says how the route was made goes in "#" comments */
static inline void kz_route_w_header(struct kz_route_writer* w, const char* path, const char* note, int tick_hz,
                                     uint32_t rng, uint32_t rng2)
{
    fprintf(w->f, "# kz-route 1: KZ_ROUTE_PLAY=%s replays it from the same start\n", path);
    if (note && *note) fprintf(w->f, "# %s\n", note);
    fprintf(w->f, "H tick_hz %d\nH rng %08x\nH rng2 %08x\n", tick_hz, rng, rng2);
}

/* F: the ticks the frame advances, the clock's tick counts, when it started
 * (ms after the first) and the two random generators */
static inline void kz_route_w_frame(struct kz_route_writer* w, int32_t n, int32_t now, int32_t prev, double ms,
                                    uint32_t rng, uint32_t rng2)
{
    fprintf(w->f, "F %d %d %d %.2f %08x %08x\n", n, now, prev, ms, rng, rng2);
    fflush(w->f);   /* a frame at a time: a run killed outright keeps all but the last */
}

/* P: a pad read's return value and its 0x24 bytes, "=" when the same as the last */
static inline void kz_route_w_pad(struct kz_route_writer* w, int32_t ret, const uint8_t* b)
{
    fprintf(w->f, "P %d ", ret);
    if (w->have_last && !memcmp(b, w->last_pad, KZ_ROUTE_PAD_BYTES)) { fputs("=\n", w->f); return; }
    for (int i = 0; i < KZ_ROUTE_PAD_BYTES; i++) fprintf(w->f, "%02x", b[i]);
    fputc('\n', w->f);
    memcpy(w->last_pad, b, KZ_ROUTE_PAD_BYTES);
    w->have_last = 1;
}

/* M: a tick's mouse look, only when the mouse moved */
static inline void kz_route_w_mouse(struct kz_route_writer* w, int dx, int dy)
{
    if (dx || dy) fprintf(w->f, "M %d %d\n", dx, dy);
}

/* S: after a player tick, the position (the floats' bits) and the generator */
static inline void kz_route_w_pos(struct kz_route_writer* w, const uint32_t pos[3], uint32_t rng)
{
    fprintf(w->f, "S %08x %08x %08x %08x\n", pos[0], pos[1], pos[2], rng);
}

#endif
