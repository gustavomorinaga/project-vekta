/*
 * Host test for port/kz1hd/native/glow_ref.c, the C glow the GPU pass (ps3recomp rsx_vk_glow.comp) is checked
 * against (the C itself is checked against the lifted SPU code by tools/mb_harness/check_glow.py). No game data.
 *  1. A uniform source v: every stage keeps it uniform (the averages and the blur's weights sum to one), so each
 *     byte of the image gains min(255, (3v) >> 1), the Modulate x1.5; at strength 128, half of that, truncated.
 *  2. A black source (alpha 255, the game's) leaves the colour as it was; strength 0 leaves the image as it was.
 *  3. Saturation: 250 + 60 stays 255.
 *  4. Mirror symmetry: the source mirrored left-right gives the result mirrored (down, blur and up are symmetric
 *     with their edges repeated), on a source of scattered dots.
 *  5. A lit dot spreads: pixels far from it gain nothing, and the glow is at most at the dot.
 */
#include "../../port/kz1hd/native/glow_ref.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
enum { F = GLOW_W * GLOW_H * 4, S = GLOW_HW * GLOW_HH * 4 };

static void fill(uint8_t* p, size_t n, uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    for (size_t i = 0; i < n; i += 4) { p[i] = a; p[i + 1] = r; p[i + 2] = g; p[i + 3] = b; }
}

int main(void)
{
    uint8_t *full = malloc(F), *src = malloc(S), *ref = malloc(F), *m = malloc(F), *msrc = malloc(S);
    srand(4242);

    /* 1. uniform source */
    const int v[4] = { 255, 40, 100, 200 }, add[4] = { 255, 60, 150, 255 };
    fill(src, S, (uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]);
    fill(full, F, 0, 10, 20, 30);
    glow_ref(full, src, GLOW_FULL);
    int bad = 0;
    for (size_t i = 0; i < F; i++) {
        const int base = (int[]){ 0, 10, 20, 30 }[i & 3], want = base + add[i & 3] > 255 ? 255 : base + add[i & 3];
        bad += full[i] != want;
    }
    CHECK(!bad, "uniform source: %d bytes not base + min(255, 3v/2)", bad);
    fill(full, F, 0, 10, 20, 30);
    glow_ref(full, src, 128);
    CHECK(full[1] == 10 + 30 && full[2] == 20 + 75 && full[3] == 30 + 127, "strength 128: %u %u %u", full[1], full[2], full[3]);

    /* 2. black source, strength 0 */
    for (size_t i = 0; i < F; i++) ref[i] = (uint8_t)rand();
    memcpy(full, ref, F);
    fill(src, S, 255, 0, 0, 0);
    glow_ref(full, src, GLOW_FULL);
    bad = 0;
    for (size_t i = 0; i < F; i++) bad += (i & 3) ? full[i] != ref[i] : full[i] != 255;
    CHECK(!bad, "black source: %d bytes changed (colour) or alpha not saturated", bad);
    for (size_t i = 0; i < S; i++) src[i] = (uint8_t)rand();
    memcpy(full, ref, F);
    glow_ref(full, src, 0);
    CHECK(!memcmp(full, ref, F), "strength 0 changed the image");

    /* 3. saturation */
    fill(src, S, 0, 40, 40, 40); fill(full, F, 0, 250, 250, 250);
    glow_ref(full, src, GLOW_FULL);
    CHECK(full[1] == 255 && full[0] == 0, "saturation: %u (alpha %u)", full[1], full[0]);

    /* 4. mirror symmetry, 5. a dot spreads */
    fill(src, S, 255, 0, 0, 0);
    for (int k = 0; k < 40; k++) {
        const int y = rand() % GLOW_HH, x = rand() % GLOW_HW;
        for (int c = 1; c < 4; c++) src[(y * GLOW_HW + x) * 4 + c] = (uint8_t)(64 + rand() % 192);
    }
    for (int y = 0; y < GLOW_HH; y++)
        for (int x = 0; x < GLOW_HW; x++) memcpy(msrc + (y * GLOW_HW + x) * 4, src + (y * GLOW_HW + GLOW_HW - 1 - x) * 4, 4);
    fill(full, F, 0, 0, 0, 0); glow_ref(full, src, GLOW_FULL);
    fill(m, F, 0, 0, 0, 0); glow_ref(m, msrc, GLOW_FULL);
    bad = 0;
    for (int y = 0; y < GLOW_H; y++)
        for (int x = 0; x < GLOW_W; x++) bad += memcmp(full + (y * GLOW_W + x) * 4, m + (y * GLOW_W + GLOW_W - 1 - x) * 4, 4) != 0;
    CHECK(!bad, "mirror symmetry: %d pixels differ", bad);

    fill(src, S, 255, 0, 0, 0);
    for (int c = 1; c < 4; c++) for (int dy = 0; dy < 4; dy++) for (int dx = 0; dx < 4; dx++)
        src[((180 + dy) * GLOW_HW + 320 + dx) * 4 + c] = 255;
    fill(full, F, 0, 0, 0, 0); glow_ref(full, src, GLOW_FULL);
    int peak = 0, far = 0;
    for (int y = 0; y < GLOW_H; y++)
        for (int x = 0; x < GLOW_W; x++) {
            const int r = full[(y * GLOW_W + x) * 4 + 1];
            if (r > peak) peak = r;
            if ((abs(x - 644) > 200 || abs(y - 364) > 200) && r) far++;
        }
    const int centre = full[(364 * GLOW_W + 644) * 4 + 1];
    CHECK(centre > 0 && centre == peak, "a dot: centre %d, peak %d", centre, peak);
    CHECK(!far, "a dot: %d pixels far from it lit", far);

    if (fails) { printf("test_glow_ref: %d failures\n", fails); return 1; }
    printf("test_glow_ref: ok\n");
    return 0;
}
