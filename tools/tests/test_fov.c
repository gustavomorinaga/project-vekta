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
