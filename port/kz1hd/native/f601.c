/*
 * Job module f60183d3d087 (jm_post): native inner loops. See native.h.
 */
#include "native.h"
#include "jobmod_f60183d3d087_e0030/spu_recomp.h"

/* 0x4E60: in place over B, per byte: min(255, B + (A * (256 - alpha)) >> 8),
 * alpha = the first byte of B's 4-byte pixel. One 16-byte block per
 * iteration; block k of B/out is at r2 - 16 + 16k, of A at r12 - 16 + 16k
 * (iteration k writes what iteration k-1 loaded; the first block was loaded
 * before the loop). r5 counts down by 4 per iteration. */
static void blend16(uint8_t* out, const uint8_t* a, const uint8_t* b)
{
    const __m128i A = _mm_loadu_si128((const __m128i*)a), B = _mm_loadu_si128((const __m128i*)b);
    const __m128i al = _mm_shuffle_epi8(B, _mm_setr_epi8(0,0,0,0, 4,4,4,4, 8,8,8,8, 12,12,12,12));
    const __m256i m = _mm256_sub_epi16(_mm256_set1_epi16(256), _mm256_cvtepu8_epi16(al));
    const __m256i s = _mm256_add_epi16(
        _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_cvtepu8_epi16(A), m), 8), _mm256_cvtepu8_epi16(B));
    _mm_storeu_si128((__m128i*)out, _mm_packus_epi16(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1)));
}

static void blend_4e60(spu_context* ctx)
{
    enum { REPLAY = 3 };
    const uint32_t cnt = ctx->gpr[5]._u32[0], n = cnt / 4;
    const uint32_t b0 = ctx->gpr[2]._u32[0] - 16, a0 = ctx->gpr[12]._u32[0] - 16;
    /* The native pass assumes whole blocks inside LS and A apart from B. */
    const int simple = !(cnt & 3) && n >= REPLAY + 2 && b0 < SPU_LS_SIZE && a0 < SPU_LS_SIZE &&
                       b0 + 16 * n <= SPU_LS_SIZE && a0 + 16 * n <= SPU_LS_SIZE &&
                       (a0 == b0 || a0 + 16 * n <= b0 || b0 + 16 * n <= a0);
    if (!simple) { jobmod_f60183d3d087_e0030_spu_func_00004E60_lifted(ctx); return; }
    uint8_t* ls = ctx->ls;
    /* Registers: the last REPLAY iterations, lifted; the B blocks they
     * overwrite before their pipeline fills are put back for the pass below. */
    const uint32_t j0 = n - REPLAY, sb = b0 + 16 * (j0 - 1);
    uint8_t save[(REPLAY + 2) * 16];
    memcpy(save, ls + sb, sizeof save);
    kz_lanes_add(ctx, 2, 16 * j0); kz_lanes_add(ctx, 12, 16 * j0); kz_lanes_add(ctx, 5, (uint32_t)-(4 * j0));
    jobmod_f60183d3d087_e0030_spu_func_00004E60_lifted(ctx);
    memcpy(ls + sb, save, sizeof save);
    for (uint32_t k = 0; k < n; k++) {
        blend16(ls + b0 + 16 * k, ls + a0 + 16 * k, ls + b0 + 16 * k);
    }
}

void jobmod_f60183d3d087_e0030_spu_func_00004E60(spu_context* ctx)
{
    kz_native_run(ctx, blend_4e60, jobmod_f60183d3d087_e0030_spu_func_00004E60_lifted, "f601:4E60");
}

/* 0x4FD0: in place over X, per byte: min(255, X + Y) -- a saturating add.
 * Block k of X/out is at r4 + 16k, of Y at r12 + 16k, for k = -2 .. n-3:
 * the loop is two stages deep, so iteration j writes what j-2 loaded and the
 * first two blocks were loaded before it. r2 trails r4 by 16. */
static void addsat_4fd0(spu_context* ctx)
{
    enum { REPLAY = 4 };
    const uint32_t cnt = ctx->gpr[5]._u32[0], n = cnt / 4;
    const uint32_t x0 = ctx->gpr[4]._u32[0] - 32, y0 = ctx->gpr[12]._u32[0] - 32;   /* block -2 */
    const int simple = !(cnt & 3) && n >= REPLAY + 2 && x0 < SPU_LS_SIZE && y0 < SPU_LS_SIZE &&
                       x0 + 16 * n <= SPU_LS_SIZE && y0 + 16 * n <= SPU_LS_SIZE &&
                       (x0 == y0 || x0 + 16 * n <= y0 || y0 + 16 * n <= x0);
    if (!simple) { jobmod_f60183d3d087_e0030_spu_func_00004FD0_lifted(ctx); return; }
    uint8_t* ls = ctx->ls;
    const uint32_t j0 = n - REPLAY, sb = x0 + 16 * j0;   /* replay writes blocks j0-2 .. n-3 */
    uint8_t save[REPLAY * 16];
    memcpy(save, ls + sb, sizeof save);
    kz_lanes_add(ctx, 2, 16 * j0); kz_lanes_add(ctx, 4, 16 * j0); kz_lanes_add(ctx, 12, 16 * j0);
    kz_lanes_add(ctx, 5, (uint32_t)-(4 * j0));
    jobmod_f60183d3d087_e0030_spu_func_00004FD0_lifted(ctx);
    memcpy(ls + sb, save, sizeof save);
    for (uint32_t k = 0; k < n; k++) {
        uint8_t* x = ls + x0 + 16 * k;
        _mm_storeu_si128((__m128i*)x, _mm_adds_epu8(_mm_loadu_si128((const __m128i*)x),
                                                    _mm_loadu_si128((const __m128i*)(ls + y0 + 16 * k))));
    }
}

void jobmod_f60183d3d087_e0030_spu_func_00004FD0(spu_context* ctx)
{
    kz_native_run(ctx, addsat_4fd0, jobmod_f60183d3d087_e0030_spu_func_00004FD0_lifted, "f601:4FD0");
}

/* 0x5368: 2x2 bilinear upsample, weights 9:3:3:1, (sum) >> 4, of 4-byte
 * pixels. Rows U (r16), M (r15), D (r13) in, two out rows (r3 above M, r21
 * below). Group g = 4 source pixels at byte 16g of each row gives 8 out
 * pixels per row: out(2s)   = (3*(3M[s] + M[s-1]) + (3X[s] + X[s-1])) >> 4,
 *                 out(2s+1) = (3*(3M[s] + M[s+1]) + (3X[s] + X[s+1])) >> 4,
 * X = U for the upper row (at r3 + 32(g+1)) and D for the lower (r21 + 32g).
 * Iteration i writes group i-1 (r7 counts down by 8); group -1 was loaded
 * by the loop's prologue, from the same rows. */
static inline __m256i u16x16(const uint8_t* p) { return _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i*)p)); }

static void upsample_group(uint8_t* ls, uint32_t u, uint32_t m, uint32_t d, uint32_t up, uint32_t lo)
{
    const __m256i three = _mm256_set1_epi16(3);
    const __m256i M = u16x16(ls + m), Ml = u16x16(ls + m - 4), Mr = u16x16(ls + m + 4);
    const __m256i U = u16x16(ls + u), Ul = u16x16(ls + u - 4), Ur = u16x16(ls + u + 4);
    const __m256i D = u16x16(ls + d), Dl = u16x16(ls + d - 4), Dr = u16x16(ls + d + 4);
    const __m256i me = _mm256_mullo_epi16(_mm256_add_epi16(_mm256_mullo_epi16(M, three), Ml), three);
    const __m256i mo = _mm256_mullo_epi16(_mm256_add_epi16(_mm256_mullo_epi16(M, three), Mr), three);
    const __m256i ue = _mm256_add_epi16(_mm256_mullo_epi16(U, three), Ul), uo = _mm256_add_epi16(_mm256_mullo_epi16(U, three), Ur);
    const __m256i de = _mm256_add_epi16(_mm256_mullo_epi16(D, three), Dl), dd = _mm256_add_epi16(_mm256_mullo_epi16(D, three), Dr);
#define KZ_PACK(v) _mm_packus_epi16(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1))
    const __m128i UE = KZ_PACK(_mm256_srli_epi16(_mm256_add_epi16(me, ue), 4));
    const __m128i UO = KZ_PACK(_mm256_srli_epi16(_mm256_add_epi16(mo, uo), 4));
    const __m128i LE = KZ_PACK(_mm256_srli_epi16(_mm256_add_epi16(me, de), 4));
    const __m128i LO = KZ_PACK(_mm256_srli_epi16(_mm256_add_epi16(mo, dd), 4));
#undef KZ_PACK
    _mm_storeu_si128((__m128i*)(ls + up),      _mm_unpacklo_epi32(UE, UO));
    _mm_storeu_si128((__m128i*)(ls + up + 16), _mm_unpackhi_epi32(UE, UO));
    _mm_storeu_si128((__m128i*)(ls + lo),      _mm_unpacklo_epi32(LE, LO));
    _mm_storeu_si128((__m128i*)(ls + lo + 16), _mm_unpackhi_epi32(LE, LO));
}

static int kz_apart(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen)
{
    return a + alen <= b || b + blen <= a;
}

static void upsample_5368(spu_context* ctx)
{
    enum { REPLAY = 3 };
    const uint32_t cnt = ctx->gpr[7]._u32[0], n = cnt / 8;
    const uint32_t u0 = ctx->gpr[16]._u32[0], m0 = ctx->gpr[15]._u32[0], d0 = ctx->gpr[13]._u32[0];
    const uint32_t up0 = ctx->gpr[3]._u32[0], lo0 = ctx->gpr[21]._u32[0];
    /* Rows are read over [base - 32, base + 16n + 16); the upper row is written
     * over [up0, up0 + 32n), the lower over [lo0 - 32, lo0 + 32n - 32). */
    const uint32_t rl = 16 * n + 48, ol = 32 * n, ub = up0, lb = lo0 - 32;
    const int simple = !(cnt & 7) && n >= REPLAY + 2 &&
        u0 >= 32 && m0 >= 32 && d0 >= 32 && lo0 >= 32 &&
        u0 + rl <= SPU_LS_SIZE && m0 + rl <= SPU_LS_SIZE && d0 + rl <= SPU_LS_SIZE &&
        ub + ol <= SPU_LS_SIZE && lb + ol <= SPU_LS_SIZE && kz_apart(ub, ol, lb, ol) &&
        kz_apart(ub, ol, u0 - 32, rl) && kz_apart(ub, ol, m0 - 32, rl) && kz_apart(ub, ol, d0 - 32, rl) &&
        kz_apart(lb, ol, u0 - 32, rl) && kz_apart(lb, ol, m0 - 32, rl) && kz_apart(lb, ol, d0 - 32, rl);
    if (!simple) { jobmod_f60183d3d087_e0030_spu_func_00005368_lifted(ctx); return; }
    /* the registers: the last REPLAY iterations, lifted */
    const uint32_t j0 = n - REPLAY;
    kz_lanes_add(ctx, 13, 16 * j0); kz_lanes_add(ctx, 15, 16 * j0); kz_lanes_add(ctx, 16, 16 * j0);
    kz_lanes_add(ctx, 3, 32 * j0); kz_lanes_add(ctx, 21, 32 * j0); kz_lanes_add(ctx, 7, (uint32_t)-(8 * j0));
    jobmod_f60183d3d087_e0030_spu_func_00005368_lifted(ctx);
    for (uint32_t g = (uint32_t)-1; g + 1 < n; g++)
        upsample_group(ctx->ls, u0 + 16 * g, m0 + 16 * g, d0 + 16 * g, up0 + 32 * (g + 1), lo0 + 32 * g);
}

void jobmod_f60183d3d087_e0030_spu_func_00005368(spu_context* ctx)
{
    kz_native_run(ctx, upsample_5368, jobmod_f60183d3d087_e0030_spu_func_00005368_lifted, "f601:5368");
}
