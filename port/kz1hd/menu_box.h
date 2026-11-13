/*
 * Which list box the mouse drives (pcoptions.cpp, menu_mouse). Header-only,
 * plain C, so a host test can check it (tools/tests/test_menu_box.c).
 *
 * A list box (vtable 0x567340) is shown while +0x38 is 1.0, and the game
 * moves its selection only while it is active: the byte at +0x41, which the
 * scripts' IsActive/GetActive read (func_004E54EC: lbz r3, 0x41(r3)) and
 * SetActive writes. The VICTORY screen's statistics are a list box that its
 * script makes inactive (MenuScript.EndGame: LISTBOX:SetActive(FALSE), with
 * the blank 7th row selected), and its Quit / Continue are a selection
 * driven left/right. Taking the inactive list as the menu in front, the mouse
 * selected the statistic under the pointer (drawn dark as a selected row)
 * and never reached Quit / Continue.
 */
#ifndef KZ_MENU_BOX_H
#define KZ_MENU_BOX_H
#include <stdint.h>

#define MENU_LIST_BOX_VTABLE 0x00567340u

/* vtable: the object's +0x00; shown: +0x38 (a float); active: the byte at +0x41 */
static inline int menu_box_drivable(uint32_t vtable, float shown, uint8_t active)
{
    return vtable == MENU_LIST_BOX_VTABLE && shown == 1.0f && active != 0;
}

/* An option row (gMenu:AddAttributeEdit / AddAttributeSlider) greyed out by
 * its script (widget:SetEnabled(FALSE); the PC screen's dependent rows,
 * pc_deps.h) has 0 in the byte at +0x40, 1 while enabled (read in the game,
 * 2026-11-13: FSR Sharpness 0 -> 1 when Upscaling went to AMD FSR 1,
 * Resolution 1 -> 0 when Display Mode went to Borderless). The pad skips such
 * a row and leaves its value alone; the mouse must too: no hover onto it, no
 * click or wheel changing it. */
#define MENU_WIDGET_ENABLED_BYTE 0x40u
static inline int menu_widget_enabled(uint8_t enabled_byte)
{
    return enabled_byte != 0;
}
#endif
