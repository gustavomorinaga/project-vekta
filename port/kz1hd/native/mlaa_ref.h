/*
 * jm_post's MLAA (Gfx.Post.MLAA, SPU module af58, with Gfx.Post.Transpose 85fc)
 * as plain C: the reference the GPU pass is checked against. Every rule comes
 * from the SPU code; docs/dev/wws-jm-post.md, "O MLAA em C".
 */
#ifndef KZ_MLAA_REF_H
#define KZ_MLAA_REF_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MLAA_W 1280
#define MLAA_H 720

/* The whole chain on a 1280x720 image, in place, 4 bytes per pixel in memory
 * order A, R, G, B (the full image the SPUs get; alpha is ignored on input).
 * On return the alpha holds the chain's flags and luma, as the SPUs leave it. */
void mlaa_ref(uint8_t* argb);

/* The SPU single-precision arithmetic the module uses, in integers (the port's
 * SPU code runs it as host IEEE ops rounding toward zero, flushing denormals). */
uint32_t mlaa_f_mul(uint32_t a, uint32_t b);                /* fm */
uint32_t mlaa_f_fma(uint32_t a, uint32_t b, uint32_t c);    /* fma: a*b + c */
uint32_t mlaa_f_fnms(uint32_t a, uint32_t b, uint32_t c);   /* fnms: c - a*b */
uint32_t mlaa_f_frest(uint32_t a);
uint32_t mlaa_f_fi(uint32_t a, uint32_t est);
uint32_t mlaa_f_csflt(int32_t v, int scale);                /* v / 2^scale */
uint32_t mlaa_f_cuflt(uint32_t v, int scale);
uint32_t mlaa_f_cfltu(uint32_t f, int scale);               /* f * 2^scale, truncated */

#ifdef __cplusplus
}
#endif
#endif
