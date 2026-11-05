/*
 * jm_post's motion blur (Gfx.Post.MotionBlur, SPU module 14e0, with the
 * Modulate 8197, HBlur ab05, VBlur 6b71, Downsample de0b and Upsample f601
 * jobs around it) as plain C: the reference the GPU passes are checked
 * against. Every rule comes from the SPU code, measured with
 * tools/mb_harness; docs/dev/wws-jm-post.md, "The motion blur outside the game and in C".
 *
 * Images are 4 bytes per pixel in memory order A, R, G, B, rows packed.
 */
#ifndef KZ_MB_REF_H
#define KZ_MB_REF_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MB_W 1280
#define MB_H 720
#define MB_HW 640
#define MB_HH 360

/* The motion field the RSX draws (640x360 at IO 0xF1000: byte 0 strength,
 * 1 and 2 the vector + 128 across and down, 3 unused), as the 14e0 jobs get
 * it: Modulate by 1.0 (a copy), then horizontal, vertical, horizontal and
 * vertical 7-tap blurs. In place. */
void mb_ref_field(uint8_t* field);

/* Gfx.Post.Downsample: the 1280x720 image to 640x360 (2x2, rounded byte averages). */
void mb_ref_down(const uint8_t* full, uint8_t* half);

/* Gfx.Post.MotionBlur: 14 taps along the field's vector from each pixel of
 * the 640x360 image, weighted by 1 + alpha/16, times the strength; the result
 * (strength in alpha) replaces the field. Taps past an edge take the edge pixel. */
void mb_ref_gather(const uint8_t* half, uint8_t* field);

/* Gfx.Post.Upsample's blend: the 640x360 result upsampled 2x (9:3:3:1, edges
 * repeated) and put over the 1280x720 image: out = up + image * (256 - up.a) / 256. In place. */
void mb_ref_blend(uint8_t* full, const uint8_t* res);

/* All of it: full image and the RSX's field in, the full image blurred. */
void mb_ref(uint8_t* full, const uint8_t* rsx_field);

#ifdef __cplusplus
}
#endif
#endif
