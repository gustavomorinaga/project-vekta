/*
 * KZ_DEBUG_GOD=1 (testing): the player takes no damage, through the game's
 * own God mode, the "Overrides" setting of the pause menu's Debug Options
 * (KZ_DEBUG_MENU): the byte at +0x131 of the debug settings at *0x00828734,
 * read by the damage code (func_002205F0). Set every frame once the settings
 * exist (route.cpp does the same for KZ_ROUTE_LOOSE), so camera and FOV
 * checks can stand in a fight without dying. Header-only so a host test can
 * check it (tools/tests/test_debug_god.c).
 */
#ifndef KZ_DEBUG_GOD_H
#define KZ_DEBUG_GOD_H
#include <stdint.h>
#include <string.h>

#define DEBUG_GOD_SETTINGS 0x00828734u   /* holds the debug settings' address */
#define DEBUG_GOD_BYTE     0x131u        /* their God mode flag */

/* KZ_DEBUG_GOD's value: on unless unset, empty or "0" */
static inline int debug_god_on(const char* e) { return e && *e && strcmp(e, "0") != 0; }

/* The flag's address for the settings pointer read at DEBUG_GOD_SETTINGS; 0 until they exist */
static inline uint32_t debug_god_flag(uint32_t settings) { return settings ? settings + DEBUG_GOD_BYTE : 0; }
#endif
