/*
 * Job module ab05f04899ea (jm_post): horizontal 7-tap blur, native. See native.h.
 *
 * Per byte, taps one 4-byte pixel apart around the centre c:
 *   0x52B0: (20c + 15(c+-1px) + 6(c+-2px) + (c+-3px)) >> 6
 *   0x5AD8: (20c + 15(c+-1px) + w * (6(c+-2px) + (c+-3px))) >> 6, at most 255,
 *           w per channel (the low half of each lane of r43).
 * Each iteration writes five 16-byte blocks; block k (k = 0 .. 5n-1, n =
 * r5 / 20) is written at out + 16k from the centre at in + 16k:
 *   0x52B0: out = r6 - 0x50, in = r11 - 0x50;  0x5AD8: out = r3, in = r10 - 0x50.
 */
#include "native.h"
#include "jobmod_ab05f04899ea_e0030/spu_recomp.h"

static inline __m256i px16(const uint8_t* p) { return _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i*)p)); }

static int hblur_ok(spu_context* ctx, uint32_t out, uint32_t in, uint32_t nblk, int r20, int r15, int r6)
{
    for (int l = 0; l < 4; l++)
        if (ctx->gpr[r20]._u32[l] != 20 || ctx->gpr[r15]._u32[l] != 15 || ctx->gpr[r6]._u32[l] != 6) return 0;
    const uint32_t len = 16 * nblk;
    return !(out & 15) && in >= 16 && out + len <= SPU_LS_SIZE && in + len + 16 <= SPU_LS_SIZE &&
           (out + len <= in - 16 || in + len + 16 <= out);
}

static void hblur(uint8_t* ls, uint32_t out, uint32_t in, uint32_t nblk, const __m256i* w)
{
    for (uint32_t k = 0; k < nblk; k++) {
        const uint8_t* c = ls + in + 16 * k;
        __m256i v = _mm256_mullo_epi16(px16(c), _mm256_set1_epi16(20));
        v = _mm256_add_epi16(v, _mm256_mullo_epi16(_mm256_add_epi16(px16(c - 4), px16(c + 4)), _mm256_set1_epi16(15)));
        __m256i o = _mm256_mullo_epi16(_mm256_add_epi16(px16(c - 8), px16(c + 8)), _mm256_set1_epi16(6));
        o = _mm256_add_epi16(o, _mm256_add_epi16(px16(c - 12), px16(c + 12)));
        v = _mm256_srli_epi16(_mm256_add_epi16(v, w ? _mm256_mullo_epi16(o, *w) : o), 6);
        _mm_store_si128((__m128i*)(ls + out + 16 * k),
                        _mm_packus_epi16(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1)));
    }
}

static void hblur_52b0(spu_context* ctx)
{
    enum { REPLAY = 2 };
    const uint32_t cnt = ctx->gpr[5]._u32[0], n = cnt / 20;
    const uint32_t out = ctx->gpr[6]._u32[0] - 0x50, in = ctx->gpr[11]._u32[0] - 0x50;
    if (cnt % 20 || n < REPLAY + 2 || !hblur_ok(ctx, out, in, 5 * n, 24, 25, 26)) {
        jobmod_ab05f04899ea_e0030_spu_func_000052B0_lifted(ctx);
        return;
    }
    const uint32_t j0 = n - REPLAY;
    kz_lanes_add(ctx, 11, 80 * j0); kz_lanes_add(ctx, 6, 80 * j0); kz_lanes_add(ctx, 5, (uint32_t)-(20 * j0));
    jobmod_ab05f04899ea_e0030_spu_func_000052B0_lifted(ctx);
    hblur(ctx->ls, out, in, 5 * n, NULL);
}

static void hblur_5ad8(spu_context* ctx)
{
    enum { REPLAY = 2 };
    const uint32_t cnt = ctx->gpr[5]._u32[0], n = cnt / 20;
    const uint32_t out = ctx->gpr[3]._u32[0], in = ctx->gpr[10]._u32[0] - 0x50;
    int ok = !(cnt % 20) && n >= REPLAY + 2 && hblur_ok(ctx, out, in, 5 * n, 56, 57, 2);
    for (int l = 0; l < 4 && ok; l++)
        ok = ctx->gpr[55]._u32[l] == 255 && (ctx->gpr[43]._u32[l] & 0xFFFF) <= 4;
    if (!ok) { jobmod_ab05f04899ea_e0030_spu_func_00005AD8_lifted(ctx); return; }
    const u128 r = ctx->gpr[43];
    const __m256i w = _mm256_setr_epi16(r._u16[0], r._u16[2], r._u16[4], r._u16[6], r._u16[0], r._u16[2], r._u16[4], r._u16[6],
                                        r._u16[0], r._u16[2], r._u16[4], r._u16[6], r._u16[0], r._u16[2], r._u16[4], r._u16[6]);
    const uint32_t j0 = n - REPLAY;
    kz_lanes_add(ctx, 10, 80 * j0); kz_lanes_add(ctx, 3, 80 * j0); kz_lanes_add(ctx, 5, (uint32_t)-(20 * j0));
    jobmod_ab05f04899ea_e0030_spu_func_00005AD8_lifted(ctx);
    hblur(ctx->ls, out, in, 5 * n, &w);
}

void jobmod_ab05f04899ea_e0030_spu_func_000052B0(spu_context* ctx)
{
    kz_native_run(ctx, hblur_52b0, jobmod_ab05f04899ea_e0030_spu_func_000052B0_lifted, "ab05:52B0");
}

void jobmod_ab05f04899ea_e0030_spu_func_00005AD8(spu_context* ctx)
{
    kz_native_run(ctx, hblur_5ad8, jobmod_ab05f04899ea_e0030_spu_func_00005AD8_lifted, "ab05:5AD8");
}
