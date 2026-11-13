/* The FOV settings (port/kz1hd/fov.h): the game's 87 degrees horizontal at 16:9 from its 56.25 vertical, the
 * settings' range, an older file's vertical `fov` carried over, the menu's rows, and that a camera's FOV is scaled
 * so a zoomed one keeps its magnification. Before, the setting was vertical and reached only the sky's camera. */
#include <math.h>
#include <stdio.h>
#include "fov.h"

static int fails;
static void near(const char* what, double got, double want, double tol)
{
    if (fabs(got - want) > tol) { fprintf(stderr, "FAIL: %s = %.4f, want %.4f\n", what, got, want); fails++; }
    else printf("ok   %s = %.4f\n", what, got);
}

int main(void)
{
    near("game horizontal", fov_game_h(), 87.08, 0.01);
    near("setting 87", fov_setting(87.0), 0.0, 0.0);
    near("setting 0", fov_setting(0.0), 0.0, 0.0);
    near("setting 50", fov_setting(50.0), 70.0, 0.0);
    near("setting 200", fov_setting(200.0), 120.0, 0.0);
    near("setting 100", fov_setting(100.0), 100.0, 0.0);
    /* an older vertical fov: 56.25 the game's, 70 vertical is ~102 horizontal, 90 vertical (121) clamps to 120 */
    near("old 56.25", fov_from_vertical(56.25), 0.0, 0.0);
    near("old 70", fov_from_vertical(70.0), 102.45, 0.01);
    near("old 90", fov_from_vertical(90.0), 120.0, 0.0);
    near("old 40", fov_from_vertical(40.0), 70.0, 0.0);
    /* the game's setting changes nothing; 120 at 16:9 is 120 */
    near("scale game", fov_scale(0.0), 1.0, 0.0);
    near("hfov at 120", 2.0 * atan(tan(fov_apply(FOV_GAME_V, fov_scale(120.0)) * 0.5 * FOV_DEG) * FOV_ASPECT) / FOV_DEG,
         120.0, 0.01);
    /* zoom: the scope's halved FOV keeps its magnification against the base view */
    {
        const double s = fov_scale(110.0);
        const double base = tan(fov_apply(56.25, s) * 0.5 * FOV_DEG), zoom = tan(fov_apply(28.125, s) * 0.5 * FOV_DEG);
        near("zoom magnification", base / zoom, tan(28.125 * FOV_DEG) / tan(14.0625 * FOV_DEG), 1e-9);
    }
    /* the call sites: the world's view (0x21A190) takes the setting with its sky (0x2B81DC) */
    near("site view", fov_site_world(0x0021A190u), 1.0, 0.0);
    near("site sky", fov_site_world(0x002B81DCu), 1.0, 0.0);
    near("site shadows", fov_site_world(0x001B53E8u), 1.0, 0.0);
    near("site other", fov_site_world(0x0024DE04u), 0.0, 0.0);
    /* the sky's near plane: unchanged at the game's 16:9 view, never moved further, and at 120 on 3440x1440 the
     * corner ray's depth of the fog ring (radius 1.68, the nearest) stays in front of it */
    {
        const double ty0 = tan(FOV_GAME_V * 0.5 * FOV_DEG);
        near("sky near 16:9", fov_sky_near(1.0, ty0 * FOV_ASPECT, ty0), 1.0, 1e-12);
        near("sky near narrower", fov_sky_near(1.0, 0.5, 0.3), 1.0, 0.0);
        const double ty = tan(fov_apply(FOV_GAME_V, fov_scale(120.0)) * 0.5 * FOV_DEG), tx = ty * 3440.0 / 1440.0;
        const double n = fov_sky_near(1.0, tx, ty), corner_depth = 1.68 / sqrt(1.0 + tx * tx + ty * ty);
        if (!(n < corner_depth)) { fprintf(stderr, "FAIL: sky near %.4f not in front of %.4f\n", n, corner_depth); fails++; }
        else printf("ok   sky near %.4f in front of the ring's corner depth %.4f\n", n, corner_depth);
        /* the game's own 16:9 corner depth margin is kept */
        near("sky near ratio", corner_depth / n, (1.68 / sqrt(1.0 + ty0 * ty0 * FOV_ASPECT * FOV_ASPECT + ty0 * ty0)) / 1.0, 1e-9);
    }
    /* the weapon (fov_weapon): the game's 60 / aspect / 0.75 is 45 at 16:9; on a widened 21:9 camera it was 30.7, and
     * comes back to 45; on a camera the port did not widen it is left alone; the setting scales it like the world's */
    {
        const double a21 = 3440.0 / 1440.0, game169 = 60.0 / FOV_ASPECT / 0.75, game21 = 60.0 / a21 / 0.75;
        near("weapon 16:9", fov_weapon(game169, FOV_ASPECT, 1, 1.0), 45.0, 1e-9);
        near("weapon 21:9 widened", fov_weapon(game21, a21, 1, 1.0), 45.0, 1e-9);
        near("weapon 21:9 not ours", fov_weapon(game21, a21, 0, 1.0), game21, 1e-9);
        near("weapon at 100 (16:9 horizontal)",
             2.0 * atan(tan(fov_weapon(game169, FOV_ASPECT, 1, fov_scale(100.0)) * 0.5 * FOV_DEG) * FOV_ASPECT) / FOV_DEG,
             2.0 * atan(tan(45.0 * 0.5 * FOV_DEG) * FOV_ASPECT * fov_scale(100.0)) / FOV_DEG, 1e-9);
    }
    /* below 16:9 (fov_narrow): the game's own 4:3 rule, vertical = 100 / aspect (func_00211540): 75 vertical and
     * 91.3 horizontal at 4:3, 62.5 at 16:10; 16:9 and wider exactly the game's; a scope's zoom keeps its
     * magnification (the 16:9 zoom halves the FOV; so does the narrowed one) */
    {
        const double a43 = 4.0 / 3.0, a1610 = 1.6, hor43 = 2.0 * atan(tan(75.0 * 0.5 * FOV_DEG) * a43) / FOV_DEG;
        near("narrow 16:9", fov_narrow(FOV_GAME_V, FOV_ASPECT), FOV_GAME_V, 0.0);
        near("narrow 21:9", fov_narrow(FOV_GAME_V, 3440.0 / 1440.0), FOV_GAME_V, 0.0);
        near("narrow none", fov_narrow(FOV_GAME_V, 0.0), FOV_GAME_V, 0.0);
        near("narrow 4:3 vertical", fov_narrow(FOV_GAME_V, a43), 100.0 / a43, 0.01);
        near("narrow 4:3 horizontal", 2.0 * atan(tan(fov_narrow(FOV_GAME_V, a43) * 0.5 * FOV_DEG) * a43) / FOV_DEG, hor43, 1e-9);
        near("narrow 4:3 is 91.3 h", hor43, 91.31, 0.01);
        near("narrow 16:10 vertical", fov_narrow(FOV_GAME_V, a1610), 100.0 / a1610, 0.01);
        near("narrow 4:3 zoom", fov_narrow(FOV_GAME_V * 0.5, a43), fov_narrow(FOV_GAME_V, a43) * 0.5, 1e-9);
        /* the weapon: the game's own 60 / aspect / 0.75 at 4:3 (60 degrees) is kept, not taken back to 16:9's 45 */
        const double game43 = 60.0 / a43 / 0.75;
        near("weapon 4:3 the game's", fov_weapon(game43, a43, 1, 1.0), 60.0, 1e-9);
    }
    /* menu rows */
    near("row 0", fov_row_value(0), 0.0, 0.0);
    near("row 1", fov_row_value(1), 70.0, 0.0);
    near("row 11", fov_row_value(11), 120.0, 0.0);
    near("row of 0", fov_row_of(0.0), 0.0, 0.0);
    near("row of 102", fov_row_of(102.0), 7.0, 0.0);
    near("row of 120", fov_row_of(120.0), 11.0, 0.0);
    for (int r = 1; r < FOV_ROWS; r++) if (fov_row_of(fov_row_value(r)) != r) { fprintf(stderr, "FAIL: row %d\n", r); fails++; }
    if (fails) return 1;
    puts("test_fov: ok");
    return 0;
}
