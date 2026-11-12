/*
 * The player's movement record at the game's 30 ticks a second
 * (native_ppu/move_velocity.cpp). Header-only, plain C, so a host test can
 * check it (tools/tests/test_move_vel.c).
 *
 * func_00294178(r3 the player controller) fills the controller's movement
 * record once a frame. With the player object at controller +0x18, it tests
 * the player's velocity (+0x80..+0x88, units a second) against an axis of
 * the player's (+0x30..+0x38, the second row of its matrix):
 *
 *     d = dot(axis, velocity)
 *     if |d| > sin(pi/4)  (0.7071: the float pi/4 at 0x294164 through
 *                          func_003C2518 with r4 = 1)
 *         d < 0 (the float 0.0 at 0x294168) ? set byte +0x66 : set byte +0x24
 *
 * and stores |velocity| at +0x28. ASSUMPTION: the axis is the direction the
 * player faces and the two bytes mean "moving forward" and "moving backward"
 * (in the logged case it was horizontal, (0.255, 0.967, 0) with z up, and the
 * player had been walking that way before a wall turned him aside); in the
 * logged run the byte was set from d = 0.723 up and clear up to d = 0.702.
 *
 * The first-person character's update (func_00229ED0) reads the record
 * through the state machine at its +0x164 (vtable +0x5C; the record is +0x1C
 * of that object, so the byte above is the record's +0x8): with the byte set
 * its +0x424 is another value (0.35 against 0.95 in the logged run), which
 * goes, times 100, into the weapon's anim variable 16 (func_001BAA98 at
 * 0x22A294), the parameter of the weapon's 1-D blend (func_00196DB8, read by
 * func_00197A90): another pair of clips, and the weapon jumps between two
 * poses.
 *
 * The velocity is the position's change over the last tick (+0x50..+0x58)
 * divided by the tick (checked in game: equal to the logged positions'
 * difference times 144). A player pushed against a wall slides along it and
 * the collision moves him by a few millimetres from tick to tick. On the PS3
 * a tick is 1/30 s and such a step changes the velocity by about 0.1; with
 * the port's game clock at 144 ticks a second the same millimetres change it
 * by about 0.5: d went 0.70, 0.86, 0.77, 0.52 in consecutive frames, the
 * byte, the blend parameter and the weapon with it changed from frame to
 * frame.
 *
 * So while that function runs, the player's velocity is the one the PS3
 * would have seen: the change of position over the last 1/30 s of game time
 * (the positions of the last ticks, kept here; between two kept ticks the
 * position is interpolated). At 30 ticks a second that is the last tick's
 * change, the game's own value, and nothing is replaced.
 *
 * This is one cause of the weapon shaking above 30 ticks a second; the
 * shaking in the dropship yard of chapter 9 has another one, not found yet
 * (docs/dev/weapon-shake-144.md).
 */
#ifndef KZ_MOVE_VEL_H
#define KZ_MOVE_VEL_H
#include <stdint.h>

#define MOVE_VEL_HZ 30          /* the game's tick rate: the velocity's window is 1/30 s */
#define MOVE_VEL_KEEP 32        /* positions kept (1/30 s is 4.8 ticks at 144 Hz) */

struct move_vel {
    uint32_t who;                     /* the player object the positions are of */
    int n;                            /* positions kept */
    int64_t tick[MOVE_VEL_KEEP];      /* the clock's tick count of each, oldest first */
    float pos[MOVE_VEL_KEEP][3];
};

/* Keeps the position of `who` at clock tick `tick`. A new object, or a tick
 * that is not after the last one (another level, a reloaded checkpoint),
 * starts over; the same tick again replaces the last position. */
static inline void move_vel_add(struct move_vel* m, uint32_t who, int64_t tick, const float pos[3])
{
    if (m->who != who || (m->n && tick < m->tick[m->n - 1])) { m->who = who; m->n = 0; }
    if (m->n && tick == m->tick[m->n - 1]) m->n--;
    if (m->n == MOVE_VEL_KEEP) {
        for (int i = 1; i < MOVE_VEL_KEEP; i++) {
            m->tick[i - 1] = m->tick[i];
            m->pos[i - 1][0] = m->pos[i][0]; m->pos[i - 1][1] = m->pos[i][1]; m->pos[i - 1][2] = m->pos[i][2];
        }
        m->n--;
    }
    m->tick[m->n] = tick;
    m->pos[m->n][0] = pos[0]; m->pos[m->n][1] = pos[1]; m->pos[m->n][2] = pos[2];
    m->n++;
}

/* The velocity over the last 1/30 s before the newest position, for a clock
 * of `hz` ticks a second, into `vel` (units a second). Returns 0, leaving
 * `vel` alone, when there is nothing to replace (hz at most 30) or not
 * enough positions to span the window yet. */
static inline int move_vel_window(const struct move_vel* m, int hz, float vel[3])
{
    if (hz <= MOVE_VEL_HZ || m->n < 2) return 0;
    const int64_t t1 = m->tick[m->n - 1];
    /* the window's start in ticks: 1/30 s before the newest, hz/30 ticks */
    const double t0 = (double)t1 - (double)hz / MOVE_VEL_HZ;
    if (t0 < (double)m->tick[0]) return 0;
    int i = m->n - 1;
    while (i > 0 && (double)m->tick[i - 1] > t0) i--;
    /* t0 lies in [tick[i-1], tick[i]] */
    const double a = (t0 - (double)m->tick[i - 1]) / (double)(m->tick[i] - m->tick[i - 1]);
    const double secs = (double)(t1 - t0) / hz;
    for (int k = 0; k < 3; k++) {
        const double p0 = m->pos[i - 1][k] + a * (m->pos[i][k] - m->pos[i - 1][k]);
        vel[k] = (float)((m->pos[m->n - 1][k] - p0) / secs);
    }
    return 1;
}
#endif
