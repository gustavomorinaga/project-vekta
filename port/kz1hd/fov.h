/*
 * The FOV settings (pcoptions.cpp) and how the cameras take them
 * (native_ppu/camera.cpp). Header-only, plain C, so a host test can check
 * them (tools/tests/test_fov.c).
 *
 * A setting is the horizontal field of view a 16:9 screen would show, in
 * degrees; a wider window shows more at the sides (Hor+). The game's camera
 * has 56.25 degrees vertical at 16:9 (100 divided by the 1280/720 aspect),
 * 87 degrees horizontal. A camera's FOV is scaled, not replaced: tan(fov/2)
 * times tan(setting/2) / tan(87/2), so a scope's zoom (the game halves the
 * cameras' FOV, 56.25 to 28.125) keeps its magnification.
 *
 *   field_of_view = 70-120, or 0: the game's (the world, its sky and the
 *                   view's culling and shadows; the first-person weapon keeps
 *                   the game's framing: its projection is made elsewhere)
 *
 * Older settings files have `fov`, the vertical FOV at 16:9 (40-90,
 * 56.25 the game's); it becomes field_of_view (fov_from_vertical).
 */
#ifndef KZ_FOV_H
#define KZ_FOV_H
#include <math.h>

#define FOV_GAME_V    56.25         /* the game's vertical FOV at 16:9 */
#define FOV_ASPECT    (16.0 / 9.0)
#define FOV_MIN       70.0
#define FOV_MAX       120.0
#define FOV_DEG       (3.14159265358979323846 / 180.0)

/* the game's horizontal FOV at 16:9: 87.08 */
static inline double fov_game_h(void) { return 2.0 * atan(tan(FOV_GAME_V * 0.5 * FOV_DEG) * FOV_ASPECT) / FOV_DEG; }

/* A setting read from the file: 0 (the game's) for 0, junk or one within
 * half a degree of the game's 87; else clamped to 70-120. */
static inline double fov_setting(double h)
{
    if (!(h > 0.0) || fabs(h - fov_game_h()) < 0.5) return 0.0;
    return h < FOV_MIN ? FOV_MIN : h > FOV_MAX ? FOV_MAX : h;
}

/* An old `fov` (vertical at 16:9) as a setting: 56.25 is the game's */
static inline double fov_from_vertical(double v)
{
    if (!(v > 0.0) || fabs(v - FOV_GAME_V) < 0.01) return 0.0;
    return fov_setting(2.0 * atan(tan(v * 0.5 * FOV_DEG) * FOV_ASPECT) / FOV_DEG);
}

/* What tan(fov/2) is multiplied by for a setting (1 for the game's) */
static inline double fov_scale(double setting)
{
    if (!(setting > 0.0)) return 1.0;
    return tan(setting * 0.5 * FOV_DEG) / tan(fov_game_h() * 0.5 * FOV_DEG);
}

/* A camera's FOV (degrees, as the game set it, zoom included) for a scale */
static inline double fov_apply(double cam_fov, double scale)
{
    return 2.0 * atan(tan(cam_fov * 0.5 * FOV_DEG) * scale) / FOV_DEG;
}

/* The sky camera's near plane (call site 0x2B81DC: near 1, far 2000) for a
 * view wider than the game's. Level 6's fog band is a ring about 2 units
 * around that camera (radius 1.68 to 2.22 in an F12 capture of 2026-11-09);
 * a point of it seen at an angle a off the view axis is cos(a) * r deep, so
 * at the screen's corners of a wide view it came closer than the near plane
 * and was clipped: hard grey edges at the sides (field_of_view 120 at
 * 3440x1440, and a little already at 87 on 21:9). The near plane shrinks by
 * how much nearer the corner rays' depth is than at the game's 16:9 view
 * (tangents tx, ty: the half view's, horizontal and vertical); it is never
 * moved further. */
static inline double fov_sky_near(double near, double tx, double ty)
{
    const double ty0 = tan(FOV_GAME_V * 0.5 * FOV_DEG), tx0 = ty0 * FOV_ASPECT;
    const double k = sqrt(1.0 + tx0 * tx0 + ty0 * ty0) / sqrt(1.0 + tx * tx + ty * ty);
    return k < 1.0 ? near * k : near;
}

/* Which setting a projection built by func_001A9FC8 takes, by its call
 * site (the return address; native_ppu/camera.cpp has how they were found):
 * the world's (the view 0x21A190, the sky and backdrop 0x2B81DC, and
 * 0x1CEC28, 0x1EE3D0, 0x1B53E8, 0xDF734, which show nothing of their own:
 * culling and shadows, it seems), or none (1). */
static inline int fov_site_world(unsigned ret)
{
    switch (ret) {
    case 0x0021A190u: case 0x002B81DCu: case 0x001CEC28u: case 0x001EE3D0u: case 0x001B53E8u: case 0x000DF734u: return 1;
    default: return 0;
    }
}

/* The menu's rows: index 0 is the game's (87), then 70, 75, ... 120 */
#define FOV_ROWS 12
static inline double fov_row_value(int row) { return row <= 0 || row >= FOV_ROWS ? 0.0 : 65.0 + 5.0 * row; }
static inline int fov_row_of(double setting)
{
    if (!(setting > 0.0)) return 0;
    int r = (int)floor((setting - 65.0) / 5.0 + 0.5);
    return r < 1 ? 1 : r > FOV_ROWS - 1 ? FOV_ROWS - 1 : r;
}
#endif
