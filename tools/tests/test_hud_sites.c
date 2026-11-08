/* HUD sites anchored by identity (port/kz1hd/native_ppu/hud_sites.h): the grenade row's two drawing sites go with
 * the ammo panel on the right whatever third of the screen their quad falls in (Rico's M224-A3 puts it in the
 * middle third, where it stayed in the 16:9 middle of an ultrawide screen); other sites keep the rule by position. */
#include <stdio.h>
#include "hud_sites.h"

int main(void)
{
    int fails = 0;
    if (hud_site_side(0x004CDC0C) != HUD_SIDE_RIGHT) { fprintf(stderr, "FAIL: 0x004CDC0C\n"); fails++; }
    if (hud_site_side(0x004CDCC0) != HUD_SIDE_RIGHT) { fprintf(stderr, "FAIL: 0x004CDCC0\n"); fails++; }
    /* the panel's own quads, the health bar and the crosshair keep the position rule */
    const unsigned others[] = { 0x004CE1D8, 0x004CD208, 0x004CCA90, 0x004CBBB4, 0x004CDAD8, 0 };
    for (int i = 0; others[i]; i++)
        if (hud_site_side(others[i]) != HUD_SIDE_BY_POSITION) { fprintf(stderr, "FAIL: 0x%08X\n", others[i]); fails++; }
    if (fails) return 1;
    puts("test_hud_sites: ok");
    return 0;
}
