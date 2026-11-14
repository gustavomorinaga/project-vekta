/*
 * Host test for port/kz1hd/native/mlaa_ref.c, the C MLAA the GPU pass is checked against. No game data.
 *  1. The SPU float helpers against the host's IEEE single precision with rounding toward zero (what the comments
 *     in mlaa_ref.h say they are): random normal operands, compared wherever the host result is a normal number.
 *  2. A few exact cases: truncation instead of rounding, denormals flushed to zero, overflow held at FLT_MAX.
 *  3. A flat image keeps its colour: with no edge there is nothing to blend.
 *  4. mlaa_ref_wh (the chain at the size the port draws the display buffer, patch 2080): at 1280x720 it is mlaa_ref;
 *     at 2560x1440 and 3440x1440 a flat image keeps its colour, a shallow slanted edge is blended along the edge and
 *     nowhere else; sizes outside [MLAA_MIN_SIDE, MLAA_MAX_SIDE] are left alone.
 */
#include "../../port/kz1hd/native/mlaa_ref.h"
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t rnd(void) { return ((uint32_t)rand() << 16) ^ (uint32_t)rand(); }
/* a normal float with an exponent in [-30, 30], so products and sums stay normal most of the time */
static uint32_t normal(void) { return (rnd() & 0x807FFFFFu) | ((uint32_t)(127 - 30 + rnd() % 61) << 23); }
static int is_normal(float f) { return isnormal(f) && fabsf(f) <= FLT_MAX; }

int main(void)
{
    srand(12345);
    fesetround(FE_TOWARDZERO);
    for (int i = 0; i < 200000; i++) {
        const uint32_t a = normal(), b = normal(), c = normal();
        volatile float fa = flt(a), fb = flt(b), fc = flt(c);
        const float m = fa * fb, f = fmaf(fa, fb, fc), n = fmaf(-fa, fb, fc);
        if (is_normal(m)) CHECK(mlaa_f_mul(a, b) == bits(m), "fm %08X*%08X: %08X, host %08X", a, b, mlaa_f_mul(a, b), bits(m));
        if (is_normal(f)) CHECK(mlaa_f_fma(a, b, c) == bits(f), "fma %08X %08X %08X: %08X, host %08X", a, b, c, mlaa_f_fma(a, b, c), bits(f));
        if (is_normal(n)) CHECK(mlaa_f_fnms(a, b, c) == bits(n), "fnms %08X %08X %08X: %08X, host %08X", a, b, c, mlaa_f_fnms(a, b, c), bits(n));
        if (fails > 10) break;
    }
    fesetround(FE_TONEAREST);

    const uint32_t one = 0x3F800000u, one_ulp = 0x3F800001u;
    CHECK(mlaa_f_mul(one, one) == one, "1*1");
    /* (1 + 2^-23)^2 = 1 + 2^-22 + 2^-46: the last term is cut, not rounded */
    CHECK(mlaa_f_mul(one_ulp, one_ulp) == 0x3F800002u, "(1+ulp)^2 = %08X", mlaa_f_mul(one_ulp, one_ulp));
    CHECK(mlaa_f_mul(0x00800000u, 0x3F000000u) == 0, "FLT_MIN/2 flushed to zero: %08X", mlaa_f_mul(0x00800000u, 0x3F000000u));
    CHECK(mlaa_f_mul(bits(FLT_MAX), 0x40000000u) == bits(FLT_MAX), "FLT_MAX*2 held: %08X", mlaa_f_mul(bits(FLT_MAX), 0x40000000u));
    CHECK(mlaa_f_mul(0, one) == 0, "0*1");
    CHECK(mlaa_f_csflt(1, 1) == 0x3F000000u, "csflt(1, 1) = 0.5: %08X", mlaa_f_csflt(1, 1));
    CHECK(mlaa_f_csflt(-3, 0) == bits(-3.0f), "csflt(-3, 0): %08X", mlaa_f_csflt(-3, 0));
    CHECK(mlaa_f_cuflt(255, 8) == bits(255.0f / 256.0f), "cuflt(255, 8): %08X", mlaa_f_cuflt(255, 8));
    CHECK(mlaa_f_cfltu(bits(1.75f), 0) == 1, "cfltu(1.75, 0) truncates: %u", mlaa_f_cfltu(bits(1.75f), 0));
    CHECK(mlaa_f_cfltu(bits(0.5f), 8) == 128, "cfltu(0.5, 8): %u", mlaa_f_cfltu(bits(0.5f), 8));

    uint8_t* img = malloc(MLAA_W * MLAA_H * 4);
    for (int i = 0; i < MLAA_W * MLAA_H; i++) { img[4 * i] = 0; img[4 * i + 1] = 90; img[4 * i + 2] = 140; img[4 * i + 3] = 200; }
    mlaa_ref(img);
    int changed = 0;
    for (int i = 0; i < MLAA_W * MLAA_H; i++) changed += img[4 * i + 1] != 90 || img[4 * i + 2] != 140 || img[4 * i + 3] != 200;
    CHECK(changed == 0, "flat image: %d pixels changed colour", changed);
    free(img);

    /* 4. mlaa_ref_wh */
    {
        const int W = MLAA_W, H = MLAA_H;
        uint8_t* a = malloc((size_t)W * H * 4); uint8_t* b = malloc((size_t)W * H * 4);
        srand(7);
        for (int i = 0; i < W * H; i++) { a[4 * i] = 0; for (int c = 1; c < 4; c++) a[4 * i + c] = 40; }
        for (int r = 0; r < 1500; r++) {                       /* random rectangles, as the GPU checks use */
            const int x = rand() % (W + 40) - 40, y = rand() % (H + 30) - 30, w = 2 + rand() % 48, h = 1 + rand() % 29;
            const uint8_t col[3] = { (uint8_t)rand(), (uint8_t)rand(), (uint8_t)rand() };
            for (int yy = y < 0 ? 0 : y; yy < y + h && yy < H; yy++)
                for (int xx = x < 0 ? 0 : x; xx < x + w && xx < W; xx++)
                    for (int c = 0; c < 3; c++) a[((long)yy * W + xx) * 4 + 1 + c] = col[c];
        }
        memcpy(b, a, (size_t)W * H * 4);
        mlaa_ref(a); mlaa_ref_wh(b, W, H);
        CHECK(!memcmp(a, b, (size_t)W * H * 4), "mlaa_ref_wh(1280, 720) differs from mlaa_ref");
        free(a); free(b);
    }
    static const int sizes[][2] = { { 2560, 1440 }, { 3440, 1440 }, { 1707, 961 } };
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        const int W = sizes[k][0], H = sizes[k][1];
        uint8_t* a = malloc((size_t)W * H * 4);
        for (int i = 0; i < W * H; i++) { a[4 * i] = 0; a[4 * i + 1] = 90; a[4 * i + 2] = 140; a[4 * i + 3] = 200; }
        mlaa_ref_wh(a, W, H);
        int changed = 0;
        for (int i = 0; i < W * H; i++) changed += a[4 * i + 1] != 90 || a[4 * i + 2] != 140 || a[4 * i + 3] != 200;
        CHECK(changed == 0, "%dx%d flat: %d pixels changed colour", W, H, changed);
        /* dark above y = H/2 + x/40 (a step every 40 columns), light below; away from the borders */
        const int x0 = 100, x1 = W - 100;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const int edge = H / 2 + (x < x0 ? x0 : x > x1 ? x1 : x) / 40 - W / 80;
                const uint8_t v = y < edge ? 30 : 220;
                uint8_t* p = a + ((long)y * W + x) * 4; p[0] = 0; p[1] = p[2] = p[3] = v;
            }
        mlaa_ref_wh(a, W, H);
        int blended = 0, stray = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const int edge = H / 2 + (x < x0 ? x0 : x > x1 ? x1 : x) / 40 - W / 80;
                const uint8_t* p = a + ((long)y * W + x) * 4;
                if (p[1] == 30 || p[1] == 220) continue;
                if (x == W - 1) continue;              /* the last column is compared with column 1 (the game's wrap) */
                if (y == edge || y == edge - 1) blended++; else stray++;
            }
        CHECK(blended > (x1 - x0) / 2, "%dx%d slanted edge: only %d pixels blended", W, H, blended);
        CHECK(stray == 0, "%dx%d slanted edge: %d pixels changed away from it", W, H, stray);
        free(a);
    }
    {
        uint8_t px[16 * 16 * 4];
        for (int i = 0; i < 16 * 16 * 4; i++) px[i] = (uint8_t)(i * 37);
        uint8_t cp[sizeof px]; memcpy(cp, px, sizeof px);
        mlaa_ref_wh(px, 16, 16);
        CHECK(!memcmp(px, cp, sizeof px), "a 16x16 image is below MLAA_MIN_SIDE and stays as it is");
    }

    printf("test_mlaa_ref: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
