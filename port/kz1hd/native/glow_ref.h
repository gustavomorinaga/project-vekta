/*
 * jm_post's glow in C: the reference the GPU pass (ps3recomp libs/video/vulkan/rsx_vk_glow.comp) is checked against.
 * Rules taken from the lifted SPU modules, stage by stage (tools/mb_harness/check_glow.py; docs/dev/wws-jm-post.md,
 * "The glow"). Pixels are 4 bytes in memory order (A, R, G, B for the game's ARGB targets); every rule treats the
 * four bytes alike.
 */
#ifndef KZ_GLOW_REF_H
#define KZ_GLOW_REF_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define GLOW_W 1280
#define GLOW_H 720
#define GLOW_HW 640
#define GLOW_HH 360
#define GLOW_QW 320
#define GLOW_QH 180

/* Strength in 1/256 of the colour added: GLOW_FULL (256) is the game's glow, bit for bit; 0 adds no colour. */
#define GLOW_FULL 256

/* Gfx.Post.Downsample (de0b): 2x2, byte averages rounded up, down the columns first. w, h: the output's size. */
void glow_ref_down(const uint8_t* in, uint8_t* out, int w, int h);

/* Gfx.Post.HBlur (ab05) then Gfx.Post.VBlur (6b71): the 7-tap binomial (1,6,15,20,15,6,1)/64 on every byte,
 * truncated to a byte after each pass, edge pixels repeated. */
void glow_ref_blur(uint8_t* img, int w, int h);

/* Gfx.Post.Modulate (8197) with factor 1.5: min(255, (x * 3) >> 1) on every byte. */
void glow_ref_mod(uint8_t* img, int w, int h);

/* Gfx.Post.Upsample's 2x (f601 0x5368): weights 9:3:3:1, (sum) >> 4, edges repeated. w, h: the input's size. */
void glow_ref_up(const uint8_t* in, uint8_t* out, int w, int h);

/* The glow texture (640x360) from the RSX's glow source (640x360 at IO 0x10000): Modulate 1.0 (a copy), halved,
 * 7 blur pairs, Modulate 1.5, 4 blur pairs, doubled back. */
void glow_ref_texture(const uint8_t* src, uint8_t* glow);

/* Upsample's composition (f601 0x4FD0): the glow texture doubled and added to the full image, saturated, every
 * byte. Strength (ours, not the game's) scales the colour bytes: (g * strength) >> 8, GLOW_FULL the game's; the alpha
 * byte (byte 0) always gets the whole add, as the game does: the MLAA leaves its flags in the alpha, this add
 * saturates it, and the motion blur after weighs its taps by it. */
void glow_ref_add(uint8_t* full, const uint8_t* glow, int strength);

/* All of it. */
void glow_ref(uint8_t* full, const uint8_t* src, int strength);

#ifdef __cplusplus
}
#endif
#endif
