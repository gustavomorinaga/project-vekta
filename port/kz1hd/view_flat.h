/*
 * When the widescreen picture goes back to the game's 16:9 with bars
 * (pcoptions.cpp, kz_view_update). Header-only, plain C, so a host test can
 * drive it with a made-up clock (tools/tests/test_view_flat.c).
 *
 * A 2D screen (menus, movies, loading) or a menu over the game (pause,
 * Failed, a character on the frontend) is shown as the game made it, 16:9:
 * its layout has pieces just off a 16:9 screen that widening would show.
 * "No 3D" was 200 ms without a 3D picture, by the clock alone, so a single
 * frame that took over 200 ms in gameplay (a hitch: 8 of them in a long
 * session's log of 2026-11-08, 230 to 381 ms, most in its slowest level)
 * looked like a 2D screen: the picture went to 16:9 and came back half a
 * second later. It now also takes VIEW_FLAT_FLIPS frames without 3D: a 2D
 * screen keeps flipping, a hitch does not flip at all.
 */
#ifndef KZ_VIEW_FLAT_H
#define KZ_VIEW_FLAT_H

#define VIEW_FLAT_NO3D_MS   200   /* no 3D picture for this long ... */
#define VIEW_FLAT_FLIPS     6     /* ... over at least this many frames */
#define VIEW_FLAT_MENU_MS   200   /* a menu layout seen this recently */
#define VIEW_WIDE_AFTER_MS  500   /* back to the full width after this much 3D */

struct view_flat {
    unsigned last_3d, last_menu, flips_at_3d;
    long long last_3d_ms, last_menu_ms, wide_since;
    int flat;
};

/* One call per main-loop pass: the clock in ms, the 3D and menu counters
 * (camera.cpp, hud.cpp) and the frames the game has asked to flip. Returns 1
 * while the picture should be 16:9. */
static inline int view_flat_update(struct view_flat* v, long long now_ms, unsigned view_3d, unsigned view_menu,
                                   unsigned flips)
{
    if (view_3d != v->last_3d) { v->last_3d = view_3d; v->last_3d_ms = now_ms; v->flips_at_3d = flips; }
    if (view_menu != v->last_menu) { v->last_menu = view_menu; v->last_menu_ms = now_ms; }
    const int no_3d = now_ms - v->last_3d_ms > VIEW_FLAT_NO3D_MS && flips - v->flips_at_3d >= VIEW_FLAT_FLIPS;
    const int flat_now = no_3d || now_ms - v->last_menu_ms < VIEW_FLAT_MENU_MS;
    /* into 16:9 at once, back to the full width after half a second of 3D
     * without a menu: a screen does not flicker between the two */
    if (flat_now) { v->flat = 1; v->wide_since = 0; }
    else if (v->flat) {
        if (!v->wide_since) v->wide_since = now_ms;
        if (now_ms - v->wide_since >= VIEW_WIDE_AFTER_MS) v->flat = 0;
    }
    return v->flat;
}
#endif
