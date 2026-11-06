/*
 * jm_post's motion blur in C (see mb_ref.h and docs/dev/wws-jm-post.md, "The motion blur outside
 * the game and in C").
 *
 * The SPU chain for one frame, after the MLAA and the glow:
 *  - the RSX's 640x360 field goes through Modulate (8197, factor 1.0: a copy)
 *    and the 7-tap binomial blur (ab05 across, 6b71 down, twice); each pass
 *    rounds down to bytes, repeats the edge pixels, and weighs the outer taps
 *    of the strength byte twice (saturated);
 *  - the full image is halved (de0b: byte averages rounded up, down the
 *    columns first);
 *  - 14e0 sums 14 taps of the half image per pixel, at +-k * vector for seven
 *    k (k across = k down * 16/9, the screen's aspect), each tap weighted by
 *    1 + alpha/16; divides by the weights' sum (the SPU's reciprocal estimate
 *    and interpolation), multiplies by strength/256 and writes the result,
 *    strength in alpha, over the field. A tap past an edge takes the edge
 *    pixel (the job's window of the image repeats it);
 *  - f601 upsamples that 2x and puts it over the full image (premultiplied).
 */
#include "mb_ref.h"
#include "mlaa_ref.h"   /* the SPU single precision in integers */
#include <stdlib.h>
#include <string.h>

/* ---------------- field: Modulate (1.0) + 4 blurs ---------------- */

static void blur7(uint8_t* img, int horizontal)
{
    const int w = MB_HW, h = MB_HH, n = horizontal ? w : h;
    uint8_t* line = malloc((size_t)n * 4);
    for (int l = 0; l < (horizontal ? h : w); l++) {
        for (int i = 0; i < n; i++) memcpy(line + 4 * i, img + 4 * (horizontal ? l * w + i : i * w + l), 4);
        for (int i = 0; i < n; i++) {
#define T(d) line[4 * (i + (d) < 0 ? 0 : i + (d) >= n ? n - 1 : i + (d)) + c]
            for (int c = 0; c < 4; c++) {
                const int outer = 6 * (T(-2) + T(2)) + T(-3) + T(3);
                int v = (20 * T(0) + 15 * (T(-1) + T(1)) + (c == 0 ? 2 : 1) * outer) >> 6;
                img[4 * (horizontal ? l * w + i : i * w + l) + c] = (uint8_t)(v > 255 ? 255 : v);
            }
#undef T
        }
    }
    free(line);
}

void mb_ref_field(uint8_t* field)
{
    blur7(field, 1); blur7(field, 0); blur7(field, 1); blur7(field, 0);
}

/* ---------------- Downsample ---------------- */

void mb_ref_down(const uint8_t* full, uint8_t* half)
{
    for (int y = 0; y < MB_HH; y++)
        for (int x = 0; x < MB_HW; x++)
            for (int c = 0; c < 4; c++) {
                const uint8_t* p = full + ((2 * y) * MB_W + 2 * x) * 4 + c;
                const int l = (p[0] + p[MB_W * 4] + 1) >> 1, r = (p[4] + p[MB_W * 4 + 4] + 1) >> 1;
                half[(y * MB_HW + x) * 4 + c] = (uint8_t)((l + r + 1) >> 1);
            }
}

/* ---------------- MotionBlur ---------------- */

static const int KD[7] = { 157, 472, 787, 1102, 1417, 1722, 2222 };   /* down, per vector unit, /65536 rows */
static const int KA[7] = { 280, 840, 1400, 1960, 2520, 3020, 4020 };  /* across, per 1/16 unit, /65536 pixels */

static int mpys(int a, int b) { return ((int16_t)a * (int16_t)b) >> 16; }   /* SPU mpys: low halves, >> 16 */

void mb_ref_gather(const uint8_t* half, uint8_t* field)
{
    const size_t n = MB_HW * MB_HH;
    uint8_t* out = malloc((size_t)n * 4);
    for (int y = 0; y < MB_HH; y++)
        for (int x = 0; x < MB_HW; x++) {
            const uint8_t* f = field + (y * MB_HW + x) * 4;
            const int down = f[2] - 128, across = (int16_t)((f[1] - 128) * 16);
            /* the taps' sums, exact: tap c * (16 + alpha) / 4096, c = 255 for the weight */
            uint32_t sum[4] = { 0, 0, 0, 0 };
            for (int k = 0; k < 7; k++)
                for (int s = 0; s < 2; s++) {
                    const int dy = mpys(s ? -down : down, KD[k]);
                    const int dx = mpys(s ? (int16_t)-across : across, KA[k]) >> 4;   /* bytes -> pixels, floor */
                    const int yy = y + dy < 0 ? 0 : y + dy >= MB_HH ? MB_HH - 1 : y + dy;
                    const int xx = x + dx < 0 ? 0 : x + dx >= MB_HW ? MB_HW - 1 : x + dx;
                    const uint8_t* p = half + (yy * MB_HW + xx) * 4;
                    const uint32_t w = 16 + p[0];
                    sum[0] += 255 * w;
                    for (int c = 1; c < 4; c++) sum[c] += p[c] * w;
                }
            const uint32_t s0 = mlaa_f_cuflt(sum[0], 12);
            const uint32_t r = mlaa_f_fi(s0, mlaa_f_frest(s0));
            const uint32_t m = mlaa_f_cuflt(f[0], 8);
            for (int c = 0; c < 4; c++) {
                const uint32_t v = mlaa_f_cfltu(mlaa_f_mul(mlaa_f_mul(mlaa_f_cuflt(sum[c], 12), r), m), 8);
                out[(y * MB_HW + x) * 4 + c] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
    memcpy(field, out, (size_t)n * 4);
    free(out);
}

/* ---------------- Upsample + blend ---------------- */

void mb_ref_blend(uint8_t* full, const uint8_t* res)
{
    for (int y = 0; y < MB_H; y++)
        for (int x = 0; x < MB_W; x++) {
            const int sy = y >> 1, sx = x >> 1;
            const int ny = (y & 1) ? (sy + 1 < MB_HH ? sy + 1 : sy) : (sy > 0 ? sy - 1 : 0);   /* row above or below */
            const int nx = (x & 1) ? (sx + 1 < MB_HW ? sx + 1 : sx) : (sx > 0 ? sx - 1 : 0);   /* left or right */
            const uint8_t *m = res + (sy * MB_HW + sx) * 4, *mx = res + (sy * MB_HW + nx) * 4;
            const uint8_t *v = res + (ny * MB_HW + sx) * 4, *vx = res + (ny * MB_HW + nx) * 4;
            uint8_t up[4];
            for (int c = 0; c < 4; c++) up[c] = (uint8_t)((3 * (3 * m[c] + mx[c]) + (3 * v[c] + vx[c])) >> 4);
            uint8_t* o = full + (y * MB_W + x) * 4;
            for (int c = 0; c < 4; c++) {
                const int r = up[c] + ((o[c] * (256 - up[0])) >> 8);
                o[c] = (uint8_t)(r > 255 ? 255 : r);
            }
        }
}

void mb_ref(uint8_t* full, const uint8_t* rsx_field)
{
    uint8_t* field = malloc(MB_HW * MB_HH * 4);
    uint8_t* half = malloc(MB_HW * MB_HH * 4);
    memcpy(field, rsx_field, MB_HW * MB_HH * 4);
    mb_ref_field(field);
    mb_ref_down(full, half);
    mb_ref_gather(half, field);
    mb_ref_blend(full, field);
    free(field); free(half);
}
