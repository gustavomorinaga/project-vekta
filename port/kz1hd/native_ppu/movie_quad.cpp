/*
 * The movie quad without its edges reaching past the frame at an internal
 * resolution scale (#158; ../movie_uv.h).
 *
 * func_0004464C(f1..f4 x, y, w, h; f5..f8 u0, v0, u1, v1; ...) draws one
 * textured 2D quad (through func_0004416C). The movie player's draw in
 * func_00278888 calls it at 0x0027939C (returns to 0x002793A0) with the
 * movie frame's coordinates in its texture (EU: 0x00279300, re/eu/address_map.tsv). That call gets its coordinates
 * pulled in by half a frame texel when the scale is not 1; every other call,
 * and that one at 1x, goes to the lifted body unchanged.
 */
#include "native_ppu.h"
#include "../movie_uv.h"

extern "C" int kz_sail_movie_size(int* w, int* h);   /* sail.cpp: the open movie's frame size */
#if defined(PS3RECOMP_RSX_VULKAN)
extern "C" void rsx_vulkan_render_scale(float* x, float* y);   /* ps3recomp patch 2091 */
#endif

void func_0004464C(ppu_context* ctx)
{
    float sx = 1.0f, sy = 1.0f;
#if defined(PS3RECOMP_RSX_VULKAN)
    rsx_vulkan_render_scale(&sx, &sy);
#endif
    int w, h;
    if (ctx->lr == KZ_GA(0x002793A0u) && kz_sail_movie_size(&w, &h)) {
        float uv[4] = {(float)ctx->fpr[5], (float)ctx->fpr[6], (float)ctx->fpr[7], (float)ctx->fpr[8]};
        if (movie_uv_inset(uv, w, h, sx, sy)) {
            ctx->fpr[5] = uv[0]; ctx->fpr[6] = uv[1]; ctx->fpr[7] = uv[2]; ctx->fpr[8] = uv[3];
        }
    }
    func_0004464C_lifted(ctx);
}
