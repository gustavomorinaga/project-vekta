/*
 * The `aspect` setting (pcoptions.cpp): the picture's aspect ratio, whatever
 * the window's. auto (the default) follows the window; a ratio shows the
 * picture at that ratio with bars where the window's differs (the frame is
 * rendered to that box: PS3RECOMP_VK_ASPECT, patch 2068), at the next start.
 * Header-only, plain C, so a host test can check it
 * (tools/tests/test_view_aspect.c).
 */
#ifndef KZ_VIEW_ASPECT_H
#define KZ_VIEW_ASPECT_H
#include <string.h>

#define VIEW_ASPECTS 6
static const char* const kViewAspect[VIEW_ASPECTS] = { "auto", "4:3", "16:10", "16:9", "21:9", "32:9" };
static const float kViewAspectValue[VIEW_ASPECTS] = { 0.0f, 4.0f / 3.0f, 1.6f, 16.0f / 9.0f, 21.0f / 9.0f, 32.0f / 9.0f };

/* The row of a setting's value; 0 (auto) for anything else */
static inline int view_aspect_row(const char* v)
{
    for (int i = 1; v && i < VIEW_ASPECTS; i++) if (!strcmp(v, kViewAspect[i])) return i;
    return 0;
}

/* The aspect a setting forces; 0 for auto */
static inline float view_aspect_value(const char* v) { return kViewAspectValue[view_aspect_row(v)]; }
#endif
