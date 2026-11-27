/* When the mouse picks a menu row (port/kz1hd/menu_hover.h): a real move does, at once; the window changing under a
 * pointer that stands still does not (the PC page's selection jumped to Frame Rate Limit after each Resolution change,
 * Windows, 2026-11-27), nor does the first spot seen once the window has settled; a move after the settle does. The
 * sizes are the ones of that session's display changes (3440x1440 fullscreen, the passing 3456x1479, 1280x800). */
#include <stdio.h>
#include "menu_hover.h"

int main(void)
{
    int fails = 0;
#define EXPECT(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)
    struct menu_hover h = { -1, -1, 0, 0, 0 };
    long long t = 100000;
    EXPECT(menu_hover_moved(&h, 900, 700, 3440, 1440, t, 0), "a menu comes up: the first spot counts (the row under the pointer, as before)");
    EXPECT(!menu_hover_moved(&h, 900, 700, 3440, 1440, t += 8, 0), "the pointer still: no move");
    EXPECT(menu_hover_moved(&h, 905, 702, 3440, 1440, t += 8, 0), "a real move: at once");

    /* Resolution 3440x1440 -> 1280x800: the menu applies it, the window shrinks a frame later, and the same pointer is
     * now at other window coordinates. */
    const long long changed = t += 8;
    EXPECT(!menu_hover_moved(&h, 905, 702, 3440, 1440, t += 8, changed), "the change asked: the old window, pointer still");
    const long long resized = t += 16;
    EXPECT(!menu_hover_moved(&h, 300, 260, 1280, 800, resized, changed), "the window resized under the pointer: no move");
    EXPECT(!menu_hover_moved(&h, 300, 260, 1280, 800, t += 8, changed), "the pointer still in the new window: no move");
    EXPECT(!menu_hover_moved(&h, 310, 262, 1280, 800, t += 8, changed), "a move inside the settle time: not taken");
    t = changed + MENU_HOVER_SETTLE_MS + 1;
    EXPECT(!menu_hover_moved(&h, 320, 266, 1280, 800, t, changed), "the settle counts from the resize, not the request");
    t = resized + MENU_HOVER_SETTLE_MS;
    EXPECT(!menu_hover_moved(&h, 320, 266, 1280, 800, t, changed), "after the settle, the pointer still: no move");
    EXPECT(menu_hover_moved(&h, 330, 270, 1280, 800, t += 8, changed), "after the settle, a real move: taken");

    /* A fullscreen switch passes through another size well after it was asked: the settle starts again there. */
    const long long fs = t += 8;
    EXPECT(!menu_hover_moved(&h, 330, 270, 1280, 800, t += 8, fs), "fullscreen asked");
    t = fs + MENU_HOVER_SETTLE_MS + 400;
    EXPECT(!menu_hover_moved(&h, 1200, 650, 3456, 1479, t, fs), "a passing size 1.15 s later: no move");
    EXPECT(!menu_hover_moved(&h, 1190, 640, 3440, 1440, t += 300, fs), "the final size: no move");
    t += MENU_HOVER_SETTLE_MS;
    EXPECT(!menu_hover_moved(&h, 1190, 640, 3440, 1440, t, fs), "settled, the pointer still: no move");
    EXPECT(menu_hover_moved(&h, 1100, 600, 3440, 1440, t += 8, fs), "settled, a real move: taken");

    /* Back to play and to a menu again: the first spot counts, unless a display change is settling. */
    menu_hover_reset(&h);
    EXPECT(menu_hover_moved(&h, 1100, 600, 3440, 1440, t += 5000, fs), "a menu after play: the first spot counts");
    if (fails) return 1;
    puts("test_menu_hover: ok");
    return 0;
}
