/*
 * jm_post's glow in C (see glow_ref.h and docs/dev/wws-jm-post.md, "The glow").
 *
 * The SPU chain for one frame (Level 8 capture, jobs in order): Modulate 8197 x24 copies the RSX's 640x360 glow
 * source (IO 0x10000) to IO 0x718000; Downsample de0b x12 halves it to 320x180 (IO 0xB7D000); HBlur ab05 / VBlur
 * 6b71 x6 each, 7 times, between IO 0xB7D000 and 0xBB5400; Modulate x6 with factor 1.5; 4 more blur pairs;
 * Upsample f601 x12 doubles it back to IO 0x718000; f601 x96 doubles that again and adds it to the full image.
 */
#include "glow_ref.h"
#include <stdlib.h>
#include <string.h>

void glow_ref_down(const uint8_t* in, uint8_t* out, int w, int h)
{
    const int iw = 2 * w;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 4; c++) {
                const uint8_t* p = in + ((2 * y) * iw + 2 * x) * 4 + c;
                const int l = (p[0] + p[iw * 4] + 1) >> 1, r = (p[4] + p[iw * 4 + 4] + 1) >> 1;
                out[(y * w + x) * 4 + c] = (uint8_t)((l + r + 1) >> 1);
            }
}

static void blur7(uint8_t* img, int w, int h, int horizontal)
{
    const int n = horizontal ? w : h, lines = horizontal ? h : w;
    uint8_t* line = malloc((size_t)n * 4);
    for (int l = 0; l < lines; l++) {
        for (int i = 0; i < n; i++) memcpy(line + 4 * i, img + 4 * (horizontal ? l * w + i : i * w + l), 4);
        for (int i = 0; i < n; i++)
            for (int c = 0; c < 4; c++) {
#define T(d) line[4 * (i + (d) < 0 ? 0 : i + (d) >= n ? n - 1 : i + (d)) + c]
                const int v = (20 * T(0) + 15 * (T(-1) + T(1)) + 6 * (T(-2) + T(2)) + T(-3) + T(3)) >> 6;
#undef T
                img[4 * (horizontal ? l * w + i : i * w + l) + c] = (uint8_t)v;   /* at most 255 */
            }
    }
    free(line);
}

void glow_ref_blur(uint8_t* img, int w, int h)
{
    blur7(img, w, h, 1);
    blur7(img, w, h, 0);
}

void glow_ref_mod(uint8_t* img, int w, int h)
{
    for (size_t i = 0; i < (size_t)w * h * 4; i++) {
        const int v = (img[i] * 3) >> 1;
        img[i] = (uint8_t)(v > 255 ? 255 : v);
    }
}

void glow_ref_up(const uint8_t* in, uint8_t* out, int w, int h)
{
    for (int y = 0; y < 2 * h; y++)
        for (int x = 0; x < 2 * w; x++) {
            const int sy = y >> 1, sx = x >> 1;
            const int ny = (y & 1) ? (sy + 1 < h ? sy + 1 : sy) : (sy > 0 ? sy - 1 : 0);
            const int nx = (x & 1) ? (sx + 1 < w ? sx + 1 : sx) : (sx > 0 ? sx - 1 : 0);
            const uint8_t *m = in + (sy * w + sx) * 4, *mx = in + (sy * w + nx) * 4;
            const uint8_t *v = in + (ny * w + sx) * 4, *vx = in + (ny * w + nx) * 4;
            for (int c = 0; c < 4; c++)
                out[(y * 2 * w + x) * 4 + c] = (uint8_t)((3 * (3 * m[c] + mx[c]) + (3 * v[c] + vx[c])) >> 4);
        }
}

void glow_ref_texture(const uint8_t* src, uint8_t* glow)
{
    uint8_t* q = malloc(GLOW_QW * GLOW_QH * 4);
    glow_ref_down(src, q, GLOW_QW, GLOW_QH);
    for (int k = 0; k < 7; k++) glow_ref_blur(q, GLOW_QW, GLOW_QH);
    glow_ref_mod(q, GLOW_QW, GLOW_QH);
    for (int k = 0; k < 4; k++) glow_ref_blur(q, GLOW_QW, GLOW_QH);
    glow_ref_up(q, glow, GLOW_QW, GLOW_QH);
    free(q);
}

void glow_ref_add(uint8_t* full, const uint8_t* glow, int strength)
{
    uint8_t* up = malloc(GLOW_W * GLOW_H * 4);
    glow_ref_up(glow, up, GLOW_HW, GLOW_HH);
    for (size_t i = 0; i < (size_t)GLOW_W * GLOW_H * 4; i++) {
        const int g = strength == GLOW_FULL ? up[i] : (up[i] * strength) >> 8;
        const int v = full[i] + g;
        full[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    free(up);
}

void glow_ref(uint8_t* full, const uint8_t* src, int strength)
{
    uint8_t* glow = malloc(GLOW_HW * GLOW_HH * 4);
    glow_ref_texture(src, glow);
    glow_ref_add(full, glow, strength);
    free(glow);
}
