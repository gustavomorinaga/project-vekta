/*
 * Which rows of the PC screen are in use (pcoptions.cpp): a row whose
 * setting depends on another one is shown greyed out while that one makes
 * it do nothing. Header-only, plain C, so a host test can check it
 * (tools/tests/test_pc_deps.c).
 *
 *   Resolution         the window's size or the fullscreen mode; borderless
 *                      always takes the desktop's
 *   Aspect Ratio       the picture's aspect with Widescreen on; off, the
 *                      picture is 16:9 with bars whatever it says
 *   HUD Position       where the HUD goes in a picture wider than 16:9;
 *                      with Widescreen off the HUD is the game's, in the
 *                      16:9 picture
 *   FSR Sharpness      RCAS's strength, only with Upscaling = AMD FSR 1
 *   Anti-aliasing      the game's MLAA; a temporal upscaler running (FSR 3.1,
 *                      DLSS) does the anti-aliasing instead and the MLAA is
 *                      not run (ps3recomp patch 2079: rsx_vulkan_temporal_active)
 *   NVIDIA Reflex      only where the driver has VK_NV_low_latency2
 *
 * The mask has one bit per row (1 << PC_DEP_*), set when the row is in use.
 */
#ifndef KZ_PC_DEPS_H
#define KZ_PC_DEPS_H
#include <string.h>

enum {
    PC_DEP_RESOLUTION, PC_DEP_ASPECT, PC_DEP_HUD_POSITION, PC_DEP_SHARPNESS,
    PC_DEP_ANTIALIASING, PC_DEP_REFLEX, PC_DEP_COUNT
};

struct pc_deps_in {
    const char* display;     /* kz1hd_pc.ini's display: window | borderless | fullscreen */
    const char* widescreen;  /* on | off */
    const char* upscaler;    /* linear | fsr */
    int temporal_running;    /* rsx_vulkan_temporal_active() */
    int reflex_available;    /* rsx_vulkan_reflex_available() */
};

static inline unsigned pc_deps_mask(const struct pc_deps_in* in)
{
    const int wide = !in->widescreen || strcmp(in->widescreen, "off") != 0;
    unsigned m = 0;
    if (!in->display || strcmp(in->display, "borderless") != 0) m |= 1u << PC_DEP_RESOLUTION;
    if (wide) m |= (1u << PC_DEP_ASPECT) | (1u << PC_DEP_HUD_POSITION);
    if (in->upscaler && !strcmp(in->upscaler, "fsr")) m |= 1u << PC_DEP_SHARPNESS;
    if (!in->temporal_running) m |= 1u << PC_DEP_ANTIALIASING;
    if (in->reflex_available) m |= 1u << PC_DEP_REFLEX;
    return m;
}
#endif
