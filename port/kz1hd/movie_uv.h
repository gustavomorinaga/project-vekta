/*
 * The movie quad's texture coordinates with an internal resolution scale (#158).
 * Header-only, plain C, for tools/tests/test_movie_uv.c.
 *
 * The game draws each movie frame as one quad (func_0004464C, called from
 * func_00278888 at 0x0027939C) over a texture larger than the frame: a
 * 1280x720 frame in a 2048x1024 linear texture, u up to 1280/2048, v up to
 * 720/1024 (movie.vfn+64 / texture.vfn+64 and movie.vfn+68 / texture.vfn+68).
 * The rows and columns past the frame are memory after the frame buffer
 * (sail.cpp; the game's allocator func_00061E44 leaves 0x80 bytes after it).
 * At 1x every pixel centre lands on a texel centre, so the bilinear filter
 * never reaches them. With an internal scale it does: the outermost row of
 * pixels took about 39% of the texel past the edge, red in one of the three
 * frame buffers, so red lines along the top and bottom at 4.5x.
 *
 * With a scale other than 1, the quad's coordinates are pulled in by half a
 * frame texel on each side, so no sample reaches past the frame. At 1x they
 * are left exactly as the game made them.
 */
#ifndef KZ_MOVIE_UV_H
#define KZ_MOVIE_UV_H

/* uv: u0, v0, u1, v1 as the game passes them; w, h: the frame's size in texels;
 * sx, sy: the internal resolution scale. Returns 1 when it changed them. */
static inline int movie_uv_inset(float uv[4], int w, int h, float sx, float sy)
{
    if ((sx == 1.0f && sy == 1.0f) || w <= 0 || h <= 0) return 0;
    const float du = 0.5f * (uv[2] - uv[0]) / (float)w, dv = 0.5f * (uv[3] - uv[1]) / (float)h;
    uv[0] += du; uv[2] -= du;
    uv[1] += dv; uv[3] -= dv;
    return 1;
}
#endif
