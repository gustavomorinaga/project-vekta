/* The movie quad's coordinates (#158, port/kz1hd/movie_uv.h): unchanged at 1x, pulled in by half a frame texel
 * with an internal scale, so the bilinear filter never reads past the 1280x720 frame in its 2048x1024 texture.
 * Then a 4.5x bilinear upscale of a frame with a red border outside it must show no red, as the game's draw did. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "movie_uv.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)

enum { TW = 64, TH = 32, FW = 40, FH = 22 };   /* a small texture and frame of the same proportions' kind */
static float tex[TH][TW];                       /* red channel: 0 in the frame, 1 outside it */

/* Bilinear sample with REPEAT, as the movie's texture is sampled. */
static float sample(float u, float v)
{
    float x = u * TW - 0.5f, y = v * TH - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y); float fx = x - x0, fy = y - y0;
    #define T(a, b) tex[(((b) % TH) + TH) % TH][(((a) % TW) + TW) % TW]
    return (1 - fy) * ((1 - fx) * T(x0, y0) + fx * T(x0 + 1, y0)) + fy * ((1 - fx) * T(x0, y0 + 1) + fx * T(x0 + 1, y0 + 1));
}

/* The worst red over a quad drawn at `scale` times the frame's size with these coordinates. */
static float worst_red(const float uv[4], float scale)
{
    const int ow = (int)(FW * scale + 0.5f), oh = (int)(FH * scale + 0.5f);
    float worst = 0;
    for (int py = 0; py < oh; py++)
        for (int px = 0; px < ow; px++) {
            const float u = uv[0] + (uv[2] - uv[0]) * (px + 0.5f) / ow, v = uv[1] + (uv[3] - uv[1]) * (py + 0.5f) / oh;
            const float r = sample(u, v);
            if (r > worst) worst = r;
        }
    return worst;
}

int main(void)
{
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) tex[y][x] = (x < FW && y < FH) ? 0.0f : 1.0f;
    const float game[4] = {0.0f, 0.0f, (float)FW / TW, (float)FH / TH};

    /* 1x: nothing changes, and the game's own coordinates already read no red */
    float uv[4]; memcpy(uv, game, sizeof uv);
    EXPECT(movie_uv_inset(uv, FW, FH, 1.0f, 1.0f) == 0);
    EXPECT(memcmp(uv, game, sizeof uv) == 0);
    EXPECT(worst_red(game, 1.0f) == 0.0f);

    /* 4.5x: the game's coordinates read red at the edges (the bug) ... */
    EXPECT(worst_red(game, 4.5f) > 0.3f);
    /* ... the inset ones do not, and they moved by half a frame texel */
    memcpy(uv, game, sizeof uv);
    EXPECT(movie_uv_inset(uv, FW, FH, 4.5f, 4.5f) == 1);
    EXPECT(fabsf(uv[0] - 0.5f / TW) < 1e-6f && fabsf(uv[2] - (FW - 0.5f) / TW) < 1e-6f);
    EXPECT(fabsf(uv[1] - 0.5f / TH) < 1e-6f && fabsf(uv[3] - (FH - 0.5f) / TH) < 1e-6f);
    EXPECT(worst_red(uv, 4.5f) < 1e-6f);

    /* any scale other than 1, either axis (DLSS's scene scale, a forced aspect's anamorphic frame) */
    memcpy(uv, game, sizeof uv); EXPECT(movie_uv_inset(uv, FW, FH, 3.0f, 3.0f) == 1 && worst_red(uv, 3.0f) < 1e-6f);
    memcpy(uv, game, sizeof uv); EXPECT(movie_uv_inset(uv, FW, FH, 1.0f, 1.5f) == 1);
    /* no frame size: left alone */
    memcpy(uv, game, sizeof uv); EXPECT(movie_uv_inset(uv, 0, 0, 4.5f, 4.5f) == 0 && memcmp(uv, game, sizeof uv) == 0);

    if (fails) return 1;
    puts("test_movie_uv: ok");
    return 0;
}
