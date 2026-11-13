/*
 * Windows display scaling: the program tells Windows it handles DPI itself
 * (per monitor, v2), so at 125 %, 150 % and so on the window, the swapchain
 * and the display modes are in real pixels. Without it Windows gives an
 * unaware program a scaled desktop (1920x1200 at 150 % looks like 1280x800)
 * and stretches the picture.
 *
 * Done through SDL2's hint SDL_HINT_WINDOWS_DPI_AWARENESS in its environment
 * form, which SDL reads when its video starts (the window, in the Vulkan
 * backend), so it must be set before that; a value already in the
 * environment wins (SDL_WINDOWS_DPI_AWARENESS=unaware|system|permonitor|
 * permonitorv2; default here permonitorv2). Window sizes stay in pixels:
 * SDL_WINDOWS_DPI_SCALING is left off. Nothing on other systems.
 * Header-only so a host test can check it (tools/tests/test_win_dpi.c).
 */
#ifndef KZ_WIN_DPI_H
#define KZ_WIN_DPI_H
#include <stdlib.h>

#define KZ_DPI_HINT "SDL_WINDOWS_DPI_AWARENESS"

static inline void kz_win_dpi_aware(void)
{
#ifdef _WIN32
    if (!getenv(KZ_DPI_HINT)) _putenv_s(KZ_DPI_HINT, "permonitorv2");
#endif
}
#endif
