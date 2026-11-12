/* The player's velocity as the movement record sees it (port/kz1hd/move_vel.h): the change of position over the last
 * 1/30 s of game time, as on the PS3, whatever the game clock's tick rate. Before, func_00294178 took the change over
 * the last tick: at 144 ticks a second the ground contact's few millimetres from tick to tick made the velocity's
 * component along the ground's normal wander across sin(pi/4) from frame to frame while walking up a slope, the record's
 * flag (+0x24) chattered and the first-person weapon jumped between two poses (chapter 9, the dropship yard). */
#include <math.h>
#include <stdio.h>
#include "move_vel.h"

static int fails;
static void check(const char* what, int ok, double got)
{
    if (!ok) { fprintf(stderr, "FAIL: %s (%.6f)\n", what, got); fails++; }
    else printf("ok   %s (%.6f)\n", what, got);
}

/* func_00294178's test: |dot(normal, velocity)| > sin(pi/4) and the dot not negative sets the flag */
static int flag(const float n[3], const float v[3])
{
    const float d = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
    return fabsf(d) > sinf(0.78539819f) && !(d < 0.0f);
}

/* the same ground jitter for every tick rate: a fixed offset per 1/1440 s of game time */
static float jitter(long t1440)
{
    unsigned x = (unsigned)t1440 * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return ((float)(x & 0xFFFF) / 65535.0f - 0.5f) * 0.003f;   /* +-1.5 mm */
}

/* Walks up a slope for 20 s at `hz` ticks a second and counts the flag's changes, with the velocity of the last tick
 * (window = 0) or of the last 1/30 s (window = 1, move_vel_window). */
static int flag_changes(int hz, int window)
{
    const float normal[3] = {0.255f, 0.967f, 0.0f};          /* the slope's, logged in game */
    const float speed[3] = {1.65f, 0.39f, 0.0f};             /* along it: dot = 0.80, above 0.7071 */
    struct move_vel m = {0};
    float last[3] = {0, 0, 0};
    int changes = 0, prev = -1;
    for (long tick = 1000; tick < 1000 + 20L * hz; tick++) {
        const double s = (double)tick / hz;
        const float pos[3] = {(float)(speed[0] * s), (float)(speed[1] * s) + jitter(tick * 1440L / hz), (float)(speed[2] * s)};
        float v[3] = {(pos[0] - last[0]) * hz, (pos[1] - last[1]) * hz, (pos[2] - last[2]) * hz};   /* the game's own */
        last[0] = pos[0]; last[1] = pos[1]; last[2] = pos[2];
        move_vel_add(&m, 0x13000000u, tick, pos);
        if (tick < 1000 + hz) continue;                     /* the first second: history and the first step */
        if (window) move_vel_window(&m, hz, v);
        const int f = flag(normal, v);
        if (prev >= 0 && f != prev) changes++;
        prev = f;
    }
    return changes;
}

int main(void)
{
    /* the bug and the fix */
    const int ps3 = flag_changes(30, 0), raw144 = flag_changes(144, 0), fix144 = flag_changes(144, 1);
    const int raw60 = flag_changes(60, 0), fix60 = flag_changes(60, 1);
    check("30 ticks a second (the PS3): the flag holds while walking up the slope", ps3 == 0, ps3);
    check("144 ticks a second, the last tick's velocity: the flag chatters (the bug)", raw144 > 20, raw144);
    check("144 ticks a second, the last 1/30 s: the flag holds", fix144 == 0, fix144);
    check("60 ticks a second, the last tick's velocity: the flag chatters", raw60 > 5, raw60);
    check("60 ticks a second, the last 1/30 s: the flag holds", fix60 == 0, fix60);

    /* at 30 ticks a second nothing is replaced */
    struct move_vel m = {0};
    float v[3] = {7, 7, 7};
    const float p0[3] = {0, 0, 0}, p1[3] = {1, 2, 3};
    move_vel_add(&m, 1, 10, p0);
    move_vel_add(&m, 1, 11, p1);
    check("30 ticks a second: no replacement", !move_vel_window(&m, 30, v) && v[0] == 7, 7);

    /* 144 ticks a second: the window is 4.8 ticks, interpolated */
    struct move_vel w = {0};
    for (int t = 0; t <= 10; t++) { const float p[3] = {(float)(t * t), 0, (float)t}; move_vel_add(&w, 1, 100 + t, p); }
    /* x = t^2: from t = 5.2 to t = 10 over 1/30 s; linear between kept ticks: x(5.2) = 25 + 0.2 * 11 */
    { struct move_vel e = {0}; const float p[3] = {0, 0, 0};
      move_vel_add(&e, 1, 1, p); move_vel_add(&e, 1, 3, p);
      check("144 ticks a second: not enough positions yet gives none", !move_vel_window(&e, 144, v), 0); }
    int got = move_vel_window(&w, 144, v);
    check("144 ticks a second: x over the last 1/30 s", got && fabsf(v[0] - (float)((100 - 27.2) * 30)) < 0.01f, v[0]);
    check("144 ticks a second: z (linear) is its rate", fabsf(v[2] - 144.0f) < 0.01f, v[2]);

    /* a frame of several ticks keeps its own tick count; the same tick again replaces the position */
    struct move_vel g = {0};
    for (int t = 0; t <= 20; t += 3) { const float p[3] = {(float)t, 0, 0}; move_vel_add(&g, 1, t, p); }
    { const float p[3] = {99, 0, 0}; move_vel_add(&g, 1, 18, p); }
    check("the same tick replaces the last position", g.n == 7 && g.pos[6][0] == 99, g.pos[6][0]);
    struct move_vel h = {0};
    for (int t = 0; t <= 21; t += 3) { const float p[3] = {(float)t, 0, 0}; move_vel_add(&h, 1, t, p); }
    got = move_vel_window(&h, 144, v);
    check("frames of 3 ticks: the rate of a straight line", got && fabsf(v[0] - 144.0f) < 0.01f, v[0]);

    /* another object or a clock that went back (a level, a checkpoint) starts over */
    move_vel_add(&g, 2, 22, p1);
    check("another object starts over", g.n == 1 && !move_vel_window(&g, 144, v), g.n);
    move_vel_add(&g, 2, 30, p1);
    move_vel_add(&g, 2, 5, p0);
    check("a clock that went back starts over", g.n == 1, g.n);

    /* more ticks than kept: the oldest go */
    struct move_vel k = {0};
    for (int t = 0; t < 100; t++) { const float p[3] = {(float)t, 0, 0}; move_vel_add(&k, 1, t, p); }
    check("the newest positions are kept", k.n == MOVE_VEL_KEEP && k.tick[MOVE_VEL_KEEP - 1] == 99 && k.tick[0] == 100 - MOVE_VEL_KEEP, k.tick[0]);
    got = move_vel_window(&k, 144, v);
    check("and give the line's rate", got && fabsf(v[0] - 144.0f) < 0.01f, v[0]);

    if (fails) { fprintf(stderr, "%d failures\n", fails); return 1; }
    printf("all ok\n");
    return 0;
}
