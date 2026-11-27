/*
 * When the mouse picks a menu row (pcoptions.cpp, menu_mouse). Header-only,
 * plain C, so a host test can check it (tools/tests/test_menu_hover.c).
 *
 * A menu selects the row under the pointer when the pointer moves. "Moved"
 * was a change of the pointer's window coordinates, and those also change
 * when the window changes under a pointer that stands still: a Display Mode
 * or Resolution change resizes or moves the window. The row that was then
 * under the pointer got selected, and the next left/right changed that row
 * instead of the one the player was on (seen 2026-11-27 on Windows: after
 * each Resolution change the selection landed on Frame Rate Limit, so the
 * presses meant for Resolution set the limit to 165, 240, Unlimited and 30).
 *
 * So a new window size, and each display change the menu applies, start a
 * settle time. During it the pointer's spot is only taken as where it rests;
 * after it, only a real move picks a row. A fullscreen switch can take a
 * second or two and pass through other sizes; each one restarts the time.
 */
#ifndef KZ_MENU_HOVER_H
#define KZ_MENU_HOVER_H

#define MENU_HOVER_SETTLE_MS 750

struct menu_hover {
    int x, y, w, h;              /* the last pointer spot and window size seen */
    long long settle_until;      /* ms: no move counts before this */
};

/* Forget the last spot (a menu came up after play): the first poll then
 * counts as a move, so the row under the pointer is taken, as before. */
static inline void menu_hover_reset(struct menu_hover* s)
{
    s->x = s->y = -1;
}

/* 1 when the pointer moved by the player's hand since the last poll. now_ms:
 * this poll's time; changed_ms: when the menu last applied a display change
 * (0: never). */
static inline int menu_hover_moved(struct menu_hover* s, int x, int y, int w, int h,
                                   long long now_ms, long long changed_ms)
{
    if (changed_ms && changed_ms + MENU_HOVER_SETTLE_MS > s->settle_until)
        s->settle_until = changed_ms + MENU_HOVER_SETTLE_MS;
    if (w != s->w || h != s->h) {
        if (s->w || s->h) s->settle_until = now_ms + MENU_HOVER_SETTLE_MS;   /* not the first size seen */
        s->w = w; s->h = h;
    }
    const int moved = x != s->x || y != s->y;
    s->x = x; s->y = y;
    return moved && now_ms >= s->settle_until;
}

#endif
