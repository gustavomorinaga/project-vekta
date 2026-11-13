/* Windows display scaling (port/kz1hd/win_dpi.h). On Windows (under Wine, tools/wine-host-tests.sh): once SDL's
 * video has started the process is per-monitor DPI aware (v2), and a value the player set first wins. Elsewhere
 * (the host test, tools/tests/test_win_dpi.sh): the environment is left alone. */
#include <stdio.h>
#include <string.h>
#include "win_dpi.h"
#ifdef _WIN32
#include <windows.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)

int main(void)
{
#ifdef _WIN32
    _putenv_s(KZ_DPI_HINT, "unaware");                     /* the player's own choice is kept */
    kz_win_dpi_aware();
    EXPECT(!strcmp(getenv(KZ_DPI_HINT), "unaware"));
    _putenv_s(KZ_DPI_HINT, "");                            /* removes it */
    kz_win_dpi_aware();
    EXPECT(getenv(KZ_DPI_HINT) && !strcmp(getenv(KZ_DPI_HINT), "permonitorv2"));
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "FAIL: SDL video: %s\n", SDL_GetError());
        return 1;
    }
    EXPECT(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    SDL_Quit();
#else
    unsetenv(KZ_DPI_HINT);
    kz_win_dpi_aware();
    EXPECT(getenv(KZ_DPI_HINT) == NULL);
#endif
    if (fails) return 1;
    puts("test_win_dpi: ok");
    return 0;
}
