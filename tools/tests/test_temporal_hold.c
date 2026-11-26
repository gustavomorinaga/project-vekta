/* rsx_temporal_hold (ps3recomp patch 2092): while held, no jitter; after it,
 * the Halton sequence again. Fails without the patch (no rsx_temporal_hold). */
#include "rsx_temporal.h"
#include <stdio.h>
#include <stdlib.h>

uint32_t cellGcm_frame_queued(void) { return 0; }
uint32_t cellGcm_frame_reached(void) { return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    float x, y;
    rsx_temporal_config(1, 2.0f / 3.0f);
    CHECK(rsx_temporal_next_jitter(&x, &y) == 1 && (x != 0.0f || y != 0.0f));
    rsx_temporal_hold(1);
    CHECK(rsx_temporal_held());
    for (int i = 0; i < 40; i++) {
        x = y = 1.0f;
        CHECK(rsx_temporal_next_jitter(&x, &y) == 0 && x == 0.0f && y == 0.0f);
    }
    rsx_temporal_hold(0);
    CHECK(!rsx_temporal_held());
    CHECK(rsx_temporal_next_jitter(&x, &y) == 1 && (x != 0.0f || y != 0.0f));
    CHECK(x >= -0.75f && x <= 0.75f && y >= -0.75f && y <= 0.75f);   /* half a render pixel, in guest pixels */
    rsx_temporal_config(0, 1.0f);
    CHECK(rsx_temporal_next_jitter(&x, &y) == 0);
    if (fails) { fprintf(stderr, "%d FAIL\n", fails); return 1; }
    puts("test_temporal_hold: ok");
    return 0;
}
