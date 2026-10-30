/*
 * Job module 6b716cb6ea57 (jm_post): vertical 7-tap blur, native. See native.h.
 *
 * Both loops read seven rows around a centre (byte offsets 0, +-s, +-2s, +-3s
 * taken from registers) one 16-byte block a step and write one block:
 *   0x5330: (20c + 15(c+s + c-s) + 6(c+2s + c-2s) + (c+3s + c-3s)) >> 6
 *   0x5880: (20c + 15(..+-s) + w * (6(..+-2s) + (..+-3s))) >> 6, at most 255,
 *           w per channel (the low half of each lane of r11).
 * Block j: centre r41 + 16j (0x5880: r49), out r3 + 16(j + 1), j = -1 .. n-2
 * (iteration i writes block i-1); r6 counts down by 4.
 */
#include "native.h"
#include "jobmod_6b716cb6ea57_e0030/spu_recomp.h"

static inline __m256i row16(const uint8_t* ls, uint32_t a) { return _mm256_cvtepu8_epi16(_mm_load_si128((const __m128i*)(ls + (a & (SPU_LS_MASK & ~15u))))); }

/* The offsets the loop takes from its registers: centre, +-s, +-2s, +-3s. */
typedef struct { uint32_t o0, p1, m1, p2, m2, p3, m3; } blur_taps;

static int blur_ok(spu_context* ctx, int c0, int rp1, int rm1, int rp2, int rm2, int rp3, int rm3,
                   int r15, int r20w, int r6w, blur_taps* t)
{
    const uint32_t s = ctx->gpr[rp1]._u32[0];
    t->o0 = ctx->gpr[c0]._u32[0];
    t->p1 = s; t->m1 = ctx->gpr[rm1]._u32[0]; t->p2 = ctx->gpr[rp2]._u32[0];
    t->m2 = ctx->gpr[rm2]._u32[0]; t->p3 = ctx->gpr[rp3]._u32[0]; t->m3 = ctx->gpr[rm3]._u32[0];
    for (int l = 0; l < 4; l++)
        if (ctx->gpr[r15]._u32[l] != 15 || ctx->gpr[r20w]._u32[l] != 20 || ctx->gpr[r6w]._u32[l] != 6) return 0;
    /* the negative offsets are stored as LS-wrapping (0x027FFD80 for -0x280) */
#define KZ_LS_EQ(a, b) ((((a) - (b)) & SPU_LS_MASK) == 0)
    return KZ_LS_EQ(t->o0, 0) && KZ_LS_EQ(t->m1, -s) && KZ_LS_EQ(t->p2, 2 * s) && KZ_LS_EQ(t->m2, -2 * s) &&
           KZ_LS_EQ(t->p3, 3 * s) && KZ_LS_EQ(t->m3, -3 * s) && !(s & 15) && s < 0x10000;
#undef KZ_LS_EQ
}

static void blur_5330(spu_context* ctx)
{
    enum { REPLAY = 3 };
    const uint32_t cnt = ctx->gpr[6]._u32[0], n = cnt / 4;
    const uint32_t c0 = ctx->gpr[41]._u32[0], o0 = ctx->gpr[3]._u32[0];
    blur_taps t;
    /* centre r18, +-s r16/r8, +-2s r20/r10, +-3s r12/r14; 15 r17, 20 r22, 6 r19 */
    const int simple = !(cnt & 3) && n >= REPLAY + 2 && !(c0 & 15) && !(o0 & 15) &&
        blur_ok(ctx, 18, 16, 8, 20, 10, 12, 14, 17, 22, 19, &t) &&
        o0 + 16 * n <= SPU_LS_SIZE &&
        (o0 + 16 * n <= c0 - 16 - 3 * t.p1 || c0 + 16 * n + 3 * t.p1 <= o0);
    if (!simple) { jobmod_6b716cb6ea57_e0030_spu_func_00005330_lifted(ctx); return; }
    const uint32_t j0 = n - REPLAY;
    kz_lanes_add(ctx, 41, 16 * j0); kz_lanes_add(ctx, 3, 16 * j0); kz_lanes_add(ctx, 6, (uint32_t)-(4 * j0));
    jobmod_6b716cb6ea57_e0030_spu_func_00005330_lifted(ctx);
    uint8_t* ls = ctx->ls;
    const uint32_t s = t.p1;
    for (uint32_t j = (uint32_t)-1; j + 1 < n; j++) {
        const uint32_t c = c0 + 16 * j;
        __m256i v = _mm256_mullo_epi16(row16(ls, c), _mm256_set1_epi16(20));
        v = _mm256_add_epi16(v, _mm256_mullo_epi16(_mm256_add_epi16(row16(ls, c + s), row16(ls, c - s)), _mm256_set1_epi16(15)));
        v = _mm256_add_epi16(v, _mm256_mullo_epi16(_mm256_add_epi16(row16(ls, c + 2 * s), row16(ls, c - 2 * s)), _mm256_set1_epi16(6)));
        v = _mm256_add_epi16(v, _mm256_add_epi16(row16(ls, c + 3 * s), row16(ls, c - 3 * s)));
        v = _mm256_srli_epi16(v, 6);
        _mm_store_si128((__m128i*)(ls + ((o0 + 16 * (j + 1)) & SPU_LS_MASK)),
                        _mm_packus_epi16(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1)));
    }
}

static void blur_5880(spu_context* ctx)
{
    enum { REPLAY = 3 };
    const uint32_t cnt = ctx->gpr[6]._u32[0], n = cnt / 4;
    const uint32_t c0 = ctx->gpr[49]._u32[0], o0 = ctx->gpr[3]._u32[0];
    blur_taps t;
    /* centre r20, +-s r21/r18, +-2s r10/r12, +-3s r14/r16; 15 r19, 20 r22, 6 r8 */
    int simple = !(cnt & 3) && n >= REPLAY + 2 && !(c0 & 15) && !(o0 & 15) &&
        blur_ok(ctx, 20, 21, 18, 10, 12, 14, 16, 19, 22, 8, &t) &&
        o0 + 16 * n <= SPU_LS_SIZE &&
        (o0 + 16 * n <= c0 - 16 - 3 * t.p1 || c0 + 16 * n + 3 * t.p1 <= o0);
    for (int l = 0; l < 4 && simple; l++)
        simple = ctx->gpr[23]._u32[l] == 255 && (ctx->gpr[11]._u32[l] & 0xFFFF) <= 4;
    if (!simple) { jobmod_6b716cb6ea57_e0030_spu_func_00005880_lifted(ctx); return; }
    const __m256i w = _mm256_setr_epi16(
        ctx->gpr[11]._u16[0], ctx->gpr[11]._u16[2], ctx->gpr[11]._u16[4], ctx->gpr[11]._u16[6],
        ctx->gpr[11]._u16[0], ctx->gpr[11]._u16[2], ctx->gpr[11]._u16[4], ctx->gpr[11]._u16[6],
        ctx->gpr[11]._u16[0], ctx->gpr[11]._u16[2], ctx->gpr[11]._u16[4], ctx->gpr[11]._u16[6],
        ctx->gpr[11]._u16[0], ctx->gpr[11]._u16[2], ctx->gpr[11]._u16[4], ctx->gpr[11]._u16[6]);
    const uint32_t j0 = n - REPLAY;
    kz_lanes_add(ctx, 49, 16 * j0); kz_lanes_add(ctx, 3, 16 * j0); kz_lanes_add(ctx, 6, (uint32_t)-(4 * j0));
    jobmod_6b716cb6ea57_e0030_spu_func_00005880_lifted(ctx);
    uint8_t* ls = ctx->ls;
    const uint32_t s = t.p1;
    for (uint32_t j = (uint32_t)-1; j + 1 < n; j++) {
        const uint32_t c = c0 + 16 * j;
        __m256i v = _mm256_mullo_epi16(row16(ls, c), _mm256_set1_epi16(20));
        v = _mm256_add_epi16(v, _mm256_mullo_epi16(_mm256_add_epi16(row16(ls, c + s), row16(ls, c - s)), _mm256_set1_epi16(15)));
        __m256i o = _mm256_mullo_epi16(_mm256_add_epi16(row16(ls, c + 2 * s), row16(ls, c - 2 * s)), _mm256_set1_epi16(6));
        o = _mm256_add_epi16(o, _mm256_add_epi16(row16(ls, c + 3 * s), row16(ls, c - 3 * s)));
        v = _mm256_srli_epi16(_mm256_add_epi16(v, _mm256_mullo_epi16(o, w)), 6);
        _mm_store_si128((__m128i*)(ls + ((o0 + 16 * (j + 1)) & SPU_LS_MASK)),
                        _mm_packus_epi16(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1)));
    }
}

void jobmod_6b716cb6ea57_e0030_spu_func_00005330(spu_context* ctx)
{
    kz_native_run(ctx, blur_5330, jobmod_6b716cb6ea57_e0030_spu_func_00005330_lifted, "6b71:5330");
}

void jobmod_6b716cb6ea57_e0030_spu_func_00005880(spu_context* ctx)
{
    kz_native_run(ctx, blur_5880, jobmod_6b716cb6ea57_e0030_spu_func_00005880_lifted, "6b71:5880");
}
