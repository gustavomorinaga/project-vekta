/* Patch 4012: the SDL controller backend turns SDL's Raw Input joystick driver
 * off on Windows (port issue #19: a built-in Xbox-style pad was opened but sent
 * no input until SDL_JOYSTICK_RAWINPUT=0), leaves it alone elsewhere, and a
 * player's SDL_JOYSTICK_RAWINPUT in the environment still wins.
 *
 * Built twice by test_pad_rawinput.sh: -DPAD_SDL_WIN32_HINTS=1 (the Windows
 * build's choice) and =0 (Linux). Only the hint step runs: no controller, no
 * window, no SDL subsystem. */
#include "cellPad.c"   /* from the patched tree, -I $PS3RECOMP_DIR/libs/input */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t a) { (void)a; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_coh_notify_write(uint32_t a) { (void)a; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
u32 cellGcm_flip_request_count(void) { return 0; }

static int hint_is(const char* want)
{
    const char* v = SDL_GetHint(SDL_HINT_JOYSTICK_RAWINPUT);
    if (!want) return v == NULL;
    return v && strcmp(v, want) == 0;
}

int main(void)
{
#ifndef SDL_HINT_JOYSTICK_RAWINPUT
    fprintf(stderr, "FAIL: SDL headers without SDL_HINT_JOYSTICK_RAWINPUT (SDL 2.0.14 or newer needed)\n");
    return 1;
#else
    unsetenv("SDL_JOYSTICK_RAWINPUT");
    pad_sdl_platform_hints();
#if PAD_SDL_WIN32_HINTS
    if (!hint_is("0")) { fprintf(stderr, "FAIL: Windows build left Raw Input on\n"); return 1; }
    /* the player's environment overrides the port's default */
    SDL_ResetHints();
    setenv("SDL_JOYSTICK_RAWINPUT", "1", 1);
    pad_sdl_platform_hints();
    if (!hint_is("1")) { fprintf(stderr, "FAIL: SDL_JOYSTICK_RAWINPUT=1 did not win\n"); return 1; }
    puts("ok: Windows turns Raw Input off; the environment still wins");
#else
    if (!hint_is(NULL)) { fprintf(stderr, "FAIL: a non-Windows build set the Raw Input hint\n"); return 1; }
    puts("ok: other platforms leave Raw Input alone");
#endif
    return 0;
#endif
}
