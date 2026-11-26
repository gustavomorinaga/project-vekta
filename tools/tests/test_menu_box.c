/* The list box the mouse drives (port/kz1hd/menu_box.h): a shown, active list box; not an inactive one (VICTORY's
 * statistics, made inactive by its script: the mouse selected the row under the pointer and never reached Quit /
 * Continue), nor a hidden one or another object. The values are the ones read in the game (2026-11-08). */
#include <stdio.h>
#include <string.h>
#include "menu_box.h"

static uint8_t mem[0x1000];
static void be32(uint32_t ea, uint32_t v) { mem[ea] = v >> 24; mem[ea + 1] = v >> 16; mem[ea + 2] = v >> 8; mem[ea + 3] = v; }

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
    /* SELECT CHARACTER on Level 1: Templar enabled, Luger / Rico / Hakha greyed (SetItemEnabled(i, FALSE)).
     * The box at 0x100, its rows at 0x300 (0x10 each), row i's cells at 0x400 + 0x20 i, cell 0's draw object at
     * 0x800 + 0x80 i, the flag at its +0x40. */
    memset(mem, 0, sizeof mem);
    be32(0x100 + MENU_LIST_ROWS, 0x300);
    for (int i = 0; i < 4; i++) {
        be32(0x300 + 0x10 * i + 0x0C, 0x400 + 0x20 * i);
        be32(0x400 + 0x20 * i + 0x08, 0x800 + 0x80 * i);
        mem[0x800 + 0x80 * i + MENU_WIDGET_ENABLED_BYTE] = i == 0;
    }
    EXPECT(menu_box_row_enabled(mem, 0x100, 0), "Templar's row (enabled)");
    EXPECT(!menu_box_row_enabled(mem, 0x100, 1), "Luger's row (greyed)");
    EXPECT(!menu_box_row_enabled(mem, 0x100, 2), "Rico's row (greyed)");
    EXPECT(!menu_box_row_enabled(mem, 0x100, 3), "Hakha's row (greyed)");
    mem[0x800 + 0x80 * 2 + MENU_WIDGET_ENABLED_BYTE] = 1;
    EXPECT(menu_box_row_enabled(mem, 0x100, 2), "Rico's row once SetItemEnabled(2, TRUE)");
    be32(0x100 + MENU_LIST_ROWS, 0);
    EXPECT(menu_box_row_enabled(mem, 0x100, 1), "a list with no row array: the mouse as before");
    if (fails) return 1;
    puts("test_menu_box: ok");
    return 0;
}
