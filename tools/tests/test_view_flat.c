/* When the widescreen picture goes to 16:9 with bars (port/kz1hd/view_flat.h): a 2D screen (frames flipping with no
 * 3D picture) and a menu over the game do; a single slow frame in gameplay (no flip at all for over 200 ms) does not.
 * Before, 200 ms without 3D by the clock alone was enough, and every hitch of a slow level made the picture shrink to
 * 16:9 and stretch back half a second later. */
#include <stdio.h>
#include "view_flat.h"

static int fails;
static void expect(const char* what, int got, int want)
{
    printf("%-62s %s\n", what, got ? "16:9" : "wide");
    if (got != want) { fprintf(stderr, "FAIL: %s\n", what); fails++; }
}

int main(void)
{
    struct view_flat v = { 0 };
    long long t = 1000; unsigned d3 = 0, menu = 0, flips = 0;
    /* gameplay at 144 fps for 2 s: a 3D picture every frame */
    for (int i = 0; i < 288; i++) { t += 7; d3++; flips++; view_flat_update(&v, t, d3, menu, flips); }
    expect("gameplay", view_flat_update(&v, t, d3, menu, flips), 0);
    /* a hitch: one frame takes 381 ms, no flip and no 3D meanwhile */
    int seen = 0;
    for (int i = 0; i < 38; i++) { t += 10; seen |= view_flat_update(&v, t, d3, menu, flips); }
    t += 1; d3++; flips++;
    seen |= view_flat_update(&v, t, d3, menu, flips);
    expect("a 381 ms hitch in gameplay", seen, 0);
    /* a movie or a loading screen: frames flip, no 3D, for 300 ms */
    seen = 0;
    for (int i = 0; i < 43; i++) { t += 7; flips++; seen |= view_flat_update(&v, t, d3, menu, flips); }
    expect("a 2D screen (300 ms of frames without 3D)", seen, 1);
    /* back to gameplay: still 16:9 for half a second, then wide */
    for (int i = 0; i < 50; i++) { t += 7; d3++; flips++; view_flat_update(&v, t, d3, menu, flips); }
    expect("350 ms of gameplay after it", view_flat_update(&v, t, d3, menu, flips), 1);
    for (int i = 0; i < 30; i++) { t += 7; d3++; flips++; view_flat_update(&v, t, d3, menu, flips); }
    expect("560 ms of gameplay after it", view_flat_update(&v, t, d3, menu, flips), 0);
    /* the pause menu over the game: 3D still drawn behind it, the menu layout every frame */
    t += 7; d3++; flips++; menu++;
    expect("a menu over the game", view_flat_update(&v, t, d3, menu, flips), 1);
    if (fails) return 1;
    puts("test_view_flat: ok");
    return 0;
}
