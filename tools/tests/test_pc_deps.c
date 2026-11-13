/* Which PC screen rows are in use (port/kz1hd/pc_deps.h): each dependent row greyed out exactly while the setting
 * it depends on makes it do nothing, and only then. */
#include <stdio.h>
#include "pc_deps.h"

static int on(const struct pc_deps_in* in, int row) { return (pc_deps_mask(in) >> row) & 1u; }

int main(void)
{
    int fails = 0;
#define EXPECT(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)
    const struct pc_deps_in def = { "window", "on", "linear", 0, 0 };
    EXPECT(on(&def, PC_DEP_RESOLUTION), "a window: Resolution in use");
    EXPECT(on(&def, PC_DEP_ASPECT) && on(&def, PC_DEP_HUD_POSITION), "widescreen on: Aspect and HUD Position in use");
    EXPECT(!on(&def, PC_DEP_SHARPNESS), "bilinear upscaling: FSR Sharpness greyed");
    EXPECT(on(&def, PC_DEP_ANTIALIASING), "no temporal upscaler: Anti-aliasing in use");
    EXPECT(!on(&def, PC_DEP_REFLEX), "no VK_NV_low_latency2: Reflex greyed");

    struct pc_deps_in in = def;
    in.display = "fullscreen"; EXPECT(on(&in, PC_DEP_RESOLUTION), "fullscreen: Resolution is the mode, in use");
    in.display = "borderless"; EXPECT(!on(&in, PC_DEP_RESOLUTION), "borderless: Resolution greyed (the desktop's size)");
    in = def; in.widescreen = "off";
    EXPECT(!on(&in, PC_DEP_ASPECT) && !on(&in, PC_DEP_HUD_POSITION), "widescreen off: Aspect and HUD Position greyed");
    EXPECT(on(&in, PC_DEP_RESOLUTION) && on(&in, PC_DEP_ANTIALIASING), "widescreen off leaves the others alone");
    in = def; in.upscaler = "fsr"; EXPECT(on(&in, PC_DEP_SHARPNESS), "AMD FSR 1: FSR Sharpness in use");
    in = def; in.temporal_running = 1; EXPECT(!on(&in, PC_DEP_ANTIALIASING), "FSR 3.1 or DLSS running: Anti-aliasing greyed");
    in = def; in.reflex_available = 1; EXPECT(on(&in, PC_DEP_REFLEX), "VK_NV_low_latency2: Reflex in use");
    in = (struct pc_deps_in){ 0, 0, 0, 0, 0 };
    EXPECT(on(&in, PC_DEP_RESOLUTION) && on(&in, PC_DEP_ASPECT) && !on(&in, PC_DEP_SHARPNESS),
           "missing keys read as the defaults (window, widescreen on, bilinear)");
    if (fails) return 1;
    puts("test_pc_deps: ok");
    return 0;
}
