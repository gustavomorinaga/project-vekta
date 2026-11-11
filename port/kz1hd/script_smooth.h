/*
 * Smooth motion for objects the level scripts move a step at a time
 * (native_ppu/script_update.cpp). Header-only, plain C, so a host test can
 * check it (tools/tests/test_script_rate.c).
 *
 * With OnUpdate at 30 a second of game time (script_rate.h), an object a
 * script moves in OnUpdate with SetTranslation (func_004940B0: position at
 * +0x50..+0x58) jumps once per 1/30 s and stands still on the frames in
 * between: at 144 fps the 11.SDDockingBay platform moved in visible jerks,
 * and the 3D sound of the four characters on it heard their speed swing
 * between zero and a peak 30 times a second, a Doppler warble that made the
 * voices sound robotic. On the PS3, at 30 fps, every frame moved it.
 *
 * An object that a script moves by a small step on two OnUpdates in a row
 * (a move it is making, not a placement) is shown one step behind and slid
 * along: the OnUpdate that sets a new position leaves it at the position it
 * set the time before (plus the part of the step already passed), and the
 * frames up to the next 1/30 s step move it there in proportion to the game
 * time passed. Anything else (a first placement, a
 * jump of SMOOTH_MAX_STEP or more, an object not moved again) is left exactly
 * as the script set it. The object always ends at the script's position.
 */
#ifndef KZ_SCRIPT_SMOOTH_H
#define KZ_SCRIPT_SMOOTH_H
#include <stdint.h>

#define SMOOTH_SLOTS 16
#define SMOOTH_MAX_STEP 4.0f   /* a longer step per 1/30 s (120 units a second) is a jump: shown as set */

struct smooth_obj {
    uint32_t obj;          /* 0 = free */
    float from[3], to[3];  /* the slide of this step */
    float last[3];         /* the position the script set last */
    int64_t set_step;      /* the 1/30 s step of that OnUpdate */
    int sliding;
};
struct smooth_state { struct smooth_obj s[SMOOTH_SLOTS]; };

static inline float smooth_abs(float v) { return v < 0 ? -v : v; }

/* A script's SetTranslation(obj, p) in the OnUpdate of 1/30 s step `step`,
 * `frac` (0..1) of the way through it. Writes the position to show now to
 * `out`. */
static inline void smooth_set(struct smooth_state* st, uint32_t obj, const float p[3], int64_t step, float frac,
                              float out[3])
{
    struct smooth_obj* e = 0;
    struct smooth_obj* free_slot = 0;
    for (int i = 0; i < SMOOTH_SLOTS; i++) {
        if (st->s[i].obj == obj) { e = &st->s[i]; break; }
        if (!st->s[i].obj && !free_slot) free_slot = &st->s[i];
    }
    for (int k = 0; k < 3; k++) out[k] = p[k];
    if (!e) {   /* first seen: shown as set, remembered */
        if (!free_slot) {   /* full: the oldest entry makes room */
            free_slot = &st->s[0];
            for (int i = 1; i < SMOOTH_SLOTS; i++) if (st->s[i].set_step < free_slot->set_step) free_slot = &st->s[i];
        }
        free_slot->obj = obj; free_slot->sliding = 0; free_slot->set_step = step;
        for (int k = 0; k < 3; k++) free_slot->last[k] = free_slot->to[k] = free_slot->from[k] = p[k];
        return;
    }
    int small = step == e->set_step + 1;   /* moved again on the next OnUpdate */
    for (int k = 0; k < 3; k++) if (!(smooth_abs(p[k] - e->last[k]) < SMOOTH_MAX_STEP)) small = 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    if (small) {   /* one step behind: slide from the last position to p, `frac` of the way now */
        for (int k = 0; k < 3; k++) {
            e->from[k] = e->last[k]; e->to[k] = p[k];
            out[k] = e->last[k] + (p[k] - e->last[k]) * frac;
        }
        e->sliding = 1;
    } else {
        e->sliding = 0;
        for (int k = 0; k < 3; k++) e->from[k] = e->to[k] = p[k];
    }
    for (int k = 0; k < 3; k++) e->last[k] = p[k];
    e->set_step = step;
}

/* A frame without OnUpdate, or before it: for each sliding object, the
 * position at `frac` (0..1) of the step after its OnUpdate, through `move`.
 * `step` is the current 1/30 s step; a slide whose step is over ends at its
 * target (the object stopped). */
static inline void smooth_frame(struct smooth_state* st, int64_t step, float frac,
                                void (*move)(uint32_t obj, const float p[3], void* user), void* user)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    for (int i = 0; i < SMOOTH_SLOTS; i++) {
        struct smooth_obj* e = &st->s[i];
        if (!e->obj || !e->sliding) continue;
        float p[3];
        const float f = step > e->set_step ? 1.0f : frac;
        for (int k = 0; k < 3; k++) p[k] = e->from[k] + (e->to[k] - e->from[k]) * f;
        move(e->obj, p, user);
        if (f >= 1.0f) { e->sliding = 0; for (int k = 0; k < 3; k++) e->from[k] = e->to[k]; }
    }
}

/* A new level or clock: forget everything. */
static inline void smooth_reset(struct smooth_state* st)
{
    for (int i = 0; i < SMOOTH_SLOTS; i++) st->s[i].obj = 0;
}
#endif
