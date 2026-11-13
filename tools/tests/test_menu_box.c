/* The list box the mouse drives (port/kz1hd/menu_box.h): a shown, active list box; not an inactive one (VICTORY's
 * statistics, made inactive by its script: the mouse selected the row under the pointer and never reached Quit /
 * Continue), nor a hidden one or another object. The values are the ones read in the game (2026-11-08). */
#include <stdio.h>
#include "menu_box.h"

int main(void)
{
    int fails = 0;
#define EXPECT(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)
    EXPECT(menu_box_drivable(0x00567340, 1.0f, 1), "the pause menu's list (active)");
    EXPECT(!menu_box_drivable(0x00567340, 1.0f, 0), "VICTORY's statistics (inactive)");
    EXPECT(!menu_box_drivable(0x00567340, 0.0f, 1), "a hidden list");
    EXPECT(!menu_box_drivable(0x005676C0, 1.0f, 1), "another kind of object");
    EXPECT(menu_widget_enabled(1), "an option row in use (+0x40 = 1)");
    EXPECT(!menu_widget_enabled(0), "a greyed-out option row (+0x40 = 0: SetEnabled(FALSE))");
    if (fails) return 1;
    puts("test_menu_box: ok");
    return 0;
}
