/*
 * HUD drawing sites anchored by identity, not by the third of the screen
 * their quad falls in (hud.cpp, func_00227A1C). Header-only, plain C, so a
 * host test can check it (tools/tests/test_hud_sites.c).
 *
 * The grenade row (the hand grenades left, func_004C6B78's quads drawn from
 * 0x4CDC08 and 0x4CDCBC) sits just left of the ammo panel, whose width
 * follows the weapon: with most weapons it falls in the right third and goes
 * to the right edge with the panel, but Rico's M224-A3 has the widest panel
 * and puts the row at x 822..867 of the 1280-wide layout, inside the middle
 * third, where it stayed in the 16:9 middle (x 2088..2158 of 3440, against
 * 2708..2778 with the M82; KZ_HUD_SITES, 2026-11-08).
 */
#ifndef KZ_HUD_SITES_H
#define KZ_HUD_SITES_H
#include <stdint.h>

enum { HUD_SIDE_BY_POSITION = -1, HUD_SIDE_RIGHT = 1 };

/* The side a drawing site (its return address) always goes with. */
static inline int hud_site_side(uint32_t ret)
{
    switch (ret) {
    case 0x004CDC0C: case 0x004CDCC0:   /* the grenade row, with the ammo panel */
        return HUD_SIDE_RIGHT;
    default:
        return HUD_SIDE_BY_POSITION;
    }
}
#endif
