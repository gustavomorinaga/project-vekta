/*
 * jm_post's MLAA in C (see mlaa_ref.h and docs/dev/wws-jm-post.md, "The MLAA in C").
 *
 * The SPU chain: Transpose (85fc) x2, MLAA (af58) x5 over the columns,
 * Transpose x2, MLAA x5 over the rows. One MLAA pass walks its lines (columns,
 * then rows) in order and, for each pair of neighbouring lines (y, y+1):
 *  - runs: the pixels of line y with the edge flag towards line y+1, cut where
 *    the edge's polarity (which side is brighter) changes;
 *  - a run of one pixel blends both lines 1/8 into each other (9930);
 *  - a run whose two ends meet a crossing edge (a "corner") gives an entry
 *    with its ends and their heights (A060); a run of more than two pixels
 *    without that takes its ends from the crossings inside it (8630);
 *  - each entry gives Z shapes (one line blended up to the middle, the other
 *    after it) or, failing those, U shapes (one line blended) (84C0), applied
 *    in that order (8000, 8288) to the lines in place, so a pair sees what the
 *    pair before it did.
 * The column pass also computes the flags (78A0) and leaves them in alpha for
 * the row pass. Each pass runs as five jobs with their own copy of their lines
 * and the line before and after; a job writes back only its own lines.
 */
#include "mlaa_ref.h"
#include <stdlib.h>
#include <string.h>

/* ---------------- SPU single precision, as the port runs it ---------------- */

typedef unsigned __int128 u128_t;

/* value = m * 2^e (m exact, any width), sign s: rounded toward zero to float,
 * results below the normal range flushed to zero, above it FLT_MAX */
static uint32_t f_pack(int s, u128_t m, int e)
{
    if (!m) return s ? 0x80000000u : 0;
    int n = 127; while (!((m >> n) & 1)) n--;               /* top bit */
    uint32_t man;
    if (n > 23) man = (uint32_t)(m >> (n - 23)); else man = (uint32_t)(m << (23 - n));
    const int be = e + n + 127;
    if (be <= 0) return s ? 0x80000000u : 0;
    if (be >= 255) return (s ? 0x80000000u : 0) | 0x7F7FFFFFu;
    return (s ? 0x80000000u : 0) | ((uint32_t)be << 23) | (man & 0x7FFFFFu);
}
/* sign, 24-bit mantissa, exponent of its lsb; 0 for zero and denormals */
static int f_split(uint32_t f, int* s, uint32_t* m, int* e)
{
    const int be = (f >> 23) & 0xFF;
    *s = f >> 31;
    if (!be) { *m = 0; *e = 0; return 0; }
    *m = (f & 0x7FFFFFu) | 0x800000u; *e = be - 150;
    return 1;
}
uint32_t mlaa_f_mul(uint32_t a, uint32_t b)
{
    int sa, sb, ea, eb; uint32_t ma, mb;
    const int za = !f_split(a, &sa, &ma, &ea), zb = !f_split(b, &sb, &mb, &eb);
    if (za || zb) return (uint32_t)(sa ^ sb) << 31;
    return f_pack(sa ^ sb, (u128_t)ma * mb, ea + eb);
}
/* exact (sp * mp * 2^ep) + (sc * mc * 2^ec), rounded toward zero */
static uint32_t f_sum(int sp, u128_t mp, int ep, int sc, u128_t mc, int ec)
{
    if (!mp) return mc ? f_pack(sc, mc, ec) : ((sp && sc) ? 0x80000000u : 0);
    if (!mc) return f_pack(sp, mp, ep);
    /* align on the smaller exponent; a term too small to align becomes a
     * sticky lsb (it only decides the direction of the truncation) */
    if (ep > ec) { if (ep - ec > 60) { mc = 1; ec = ep - 61; } mp <<= (ep - ec); ep = ec; }
    else if (ec > ep) { if (ec - ep > 60) { mp = 1; ep = ec - 61; } mc <<= (ec - ep); ec = ep; }
    if (sp == sc) return f_pack(sp, mp + mc, ep);
    if (mp == mc) return 0;                                /* exact zero: +0 toward zero */
    return mp > mc ? f_pack(sp, mp - mc, ep) : f_pack(sc, mc - mp, ep);
}
uint32_t mlaa_f_fma(uint32_t a, uint32_t b, uint32_t c)
{
    int sa, sb, sc, ea, eb, ec; uint32_t ma, mb, mc;
    const int za = !f_split(a, &sa, &ma, &ea), zb = !f_split(b, &sb, &mb, &eb);
    f_split(c, &sc, &mc, &ec);
    if (za || zb) return f_sum(sa ^ sb, 0, 0, sc, mc, ec);
    return f_sum(sa ^ sb, (u128_t)ma * mb, ea + eb, sc, mc, ec);
}
uint32_t mlaa_f_fnms(uint32_t a, uint32_t b, uint32_t c) { return mlaa_f_fma(a ^ 0x80000000u, b, c); }
static const uint32_t frest_lut[32] = {   /* SPU frest fraction table (as the runtime has it) */
    0x7FFBE0, 0x7F87A6, 0x70EF72, 0x708B40, 0x638B12, 0x633AEA, 0x5792C4, 0x574AA0,
    0x4CCA7E, 0x4C9262, 0x430A44, 0x42D62A, 0x3A2E12, 0x39FDFA, 0x3215E4, 0x31F1D2,
    0x2AA9BE, 0x2A85AC, 0x23D59A, 0x23BD8E, 0x1D8576, 0x1D8576, 0x17AD5A, 0x17AD5A,
    0x124543, 0x124543, 0x0D392D, 0x0D392D, 0x08851A, 0x08851A, 0x041D07, 0x041D07 };
uint32_t mlaa_f_frest(uint32_t a)
{
    const uint32_t e = (a >> 23) & 0xFF, rexp = !e ? 255 : e >= 253 ? 0 : 253 - e;
    return (a & 0x80000000u) | (rexp << 23) | (frest_lut[(a >> 18) & 0x1F] & 0x7FFFFFu);
}
uint32_t mlaa_f_fi(uint32_t a, uint32_t est)
{
    int e = (est >> 23) & 0xFF;
    if (!e) return 0;
    uint64_t m = ((((uint64_t)1 << 13) | ((est >> 10) & 0x1FFF)) << 19) - (uint64_t)(est & 0x3FF) * (a & 0x7FFFF);
    if (m < ((uint64_t)1 << 32)) { m <<= 1; if (--e == 0) return 0; }
    return (est & 0x80000000u) | ((uint32_t)e << 23) | (uint32_t)((m >> 9) & 0x7FFFFFu);
}
uint32_t mlaa_f_csflt(int32_t v, int scale)
{
    if (!v) return 0;
    return f_pack(v < 0, v < 0 ? (u128_t)(0u - (uint32_t)v) : (u128_t)(uint32_t)v, -scale);
}
uint32_t mlaa_f_cuflt(uint32_t v, int scale) { return v ? f_pack(0, v, -scale) : 0; }
uint32_t mlaa_f_cfltu(uint32_t f, int scale)
{
    int s, e; uint32_t m;
    if (!f_split(f, &s, &m, &e) || s) return 0;
    const int sh = e + scale;
    if (sh >= 9) return 0xFFFFFFFFu;
    return sh >= 0 ? m << sh : sh <= -24 ? 0 : m >> -sh;
}

/* 16-bit height num/den (A060, 8630: csflt, frest, fi, one Newton step with
 * 1.0000001, fm), or -1 when it is not in [0, 1) */
static int height_q(int32_t num, int32_t den)
{
    if (!den) return -1;
    const uint32_t fn = mlaa_f_csflt(num * 256, 0), fd = mlaa_f_csflt(den * 256, 0);
    const uint32_t r = mlaa_f_fi(fd, mlaa_f_frest(fd));
    const uint32_t q = mlaa_f_mul(mlaa_f_fma(mlaa_f_fnms(fd, r, 0x3F800001u), r, r), fn);
    if (q >= 0x3F800000u) return -1;          /* >= 1.0, negative (even -0) */
    return (int)mlaa_f_cfltu(q, 16);
}
/* 8000/8288: the per-pixel step of a half, (2 - 2h) / L in 16.16 */
static uint32_t slope(uint32_t h, uint32_t L)
{
    const uint32_t a = mlaa_f_fnms(mlaa_f_cuflt(h, 16), 0x40000000u, 0x40000000u);
    const uint32_t lf = mlaa_f_cuflt(L, 0);
    return mlaa_f_cfltu(mlaa_f_mul(a, mlaa_f_fi(lf, mlaa_f_frest(lf))), 16);
}

/* ---------------- lines ---------------- */

/* A pass sees its image as lines of pixels: line l, position p is the pixel
 * at base + l * lstep + p * pstep. Flags (alpha) in the pass's own roles: */
enum { F_EDGE = 0x40, F_CROSS = 0x80, F_POL = 0x10 };
typedef struct {
    uint8_t* px;            /* colour, 4 bytes per pixel (alpha ignored here) */
    long     lstep, pstep;
    int      nlines, len;
    const uint8_t* al;      /* flags: nal lines of len (row-major), roles as above */
    int      nal;
} lines_t;

static inline int AL(const lines_t* L, int l, int p)
{
    if (p >= L->len) { l++; p -= L->len; }       /* past the end: the next line's first pixel */
    if (l < 0 || l >= L->nal || p < 0) return 0;
    return L->al[(long)l * L->len + p];
}
static inline uint8_t* PX(const lines_t* L, int l, int p) { return L->px + l * L->lstep + p * L->pstep; }

/* an entry: ends (positions, -1 none) a0 a1 on line y, b0 b1 on line y+1, and their heights */
typedef struct { int pos[4]; int h[4]; } entry_t;   /* AL, AR, BL, BR */

static int lum(int a) { return a & 15; }
/* lane formula of A060/8630 at an end: (diag, num, den) for multiplier M */
static void lane_q(const lines_t* L, int y, int lane, int pos, int M, int* d, int* num, int* den)
{
    const int A = y, B = y + 1, Ym = y - 1, Yp = y + 2;
    int r76, r75, r77;
    if (lane == 0) { const int s = pos, sm = s > 0 ? s - 1 : 0;
        r76 = lum(AL(L, A, s)) - lum(AL(L, B, s)); r75 = lum(AL(L, Ym, sm)) - lum(AL(L, A, sm));
        r77 = lum(AL(L, Ym, sm)) - lum(AL(L, B, s)); *d = (AL(L, Ym, sm) & F_EDGE) != 0;
    } else if (lane == 2) { const int s = pos, sm = s > 0 ? s - 1 : 0;
        r76 = lum(AL(L, B, s)) - lum(AL(L, A, s)); r75 = lum(AL(L, Yp, sm)) - lum(AL(L, B, sm));
        r77 = lum(AL(L, Yp, sm)) - lum(AL(L, A, s)); *d = (AL(L, B, sm) & F_EDGE) != 0;
    } else if (lane == 1) { const int e = pos;
        r76 = lum(AL(L, A, e + 1)) - lum(AL(L, Ym, e + 1)); r75 = lum(AL(L, B, e)) - lum(AL(L, A, e));
        r77 = lum(AL(L, B, e)) - lum(AL(L, Ym, e + 1)); *d = (AL(L, Ym, e + 1) & F_EDGE) != 0;
    } else { const int e = pos;
        r76 = lum(AL(L, B, e + 1)) - lum(AL(L, Yp, e + 1)); r75 = lum(AL(L, A, e)) - lum(AL(L, B, e));
        r77 = lum(AL(L, A, e)) - lum(AL(L, Yp, e + 1)); *d = (AL(L, B, e + 1) & F_EDGE) != 0;
    }
    *num = r77 * M + (r76 - r75);
    *den = (r76 + r75) * M + (r76 - r75);
}
static int lane_h(const lines_t* L, int y, int lane, int pos, int M)
{
    int d, num, den;
    lane_q(L, y, lane, pos, M, &d, &num, &den);
    return d ? height_q(num, den) : -1;
}

/* A060: a run with a corner at each end; 0 if it has not */
static int entry_corners(const lines_t* L, int y, int x0, int n, entry_t* e)
{
    const int x1 = x0 + n - 1, xl = x0 > 0 ? x0 - 1 : 0;
    const int c[4] = { (AL(L, y, xl) & F_CROSS) != 0, (AL(L, y, x1) & F_CROSS) != 0,
                       (AL(L, y + 1, xl) & F_CROSS) != 0, (AL(L, y + 1, x1) & F_CROSS) != 0 };
    if (!((c[0] || c[2]) && (c[1] || c[3]))) return 0;
    const int pos[4] = { x0, x1, x0, x1 };
    for (int k = 0; k < 4; k++) {
        e->pos[k] = c[k] ? pos[k] : -1;
        const int h = c[k] ? lane_h(L, y, k == 0 ? 0 : k == 1 ? 1 : k == 2 ? 2 : 3, pos[k], n) : -1;
        e->h[k] = h >= 0 ? h : 0x8000;
    }
    return 1;
}
/* 8630: a run of more than two pixels without corners at both ends. For each
 * end, scanning the run: a start (left) at s = x where the crossing is at
 * x - 1, an end (right) at e = x + 1 where it is at x + 1, x up to the last
 * but one; the first start / last end whose height is usable wins, else the
 * first start / last end at all (height 0.5) */
static void entry_crossings(const lines_t* L, int y, int x0, int n, entry_t* e)
{
    const int x1 = x0 + n - 1;
    int anyp[4] = { -1, -1, -1, -1 }, valp[4] = { -1, -1, -1, -1 }, valh[4] = { 0 };
    for (int k = 0; k < n; k++) {
        const int x = x0 + k;
        for (int lane = 0; lane < 4; lane++) {
            const int r = (lane < 2) ? y : y + 1, left = (lane == 0 || lane == 2);
            int xc, pos, M;
            if (left) { xc = x > 0 ? x - 1 : 0; pos = x; M = n - k; }
            else { if (k == n - 1) continue; xc = x + 1; pos = x + 1; M = k + 2; }
            if (!(AL(L, r, xc) & F_CROSS)) continue;
            const int h = lane_h(L, y, lane, pos, M);
            if (anyp[lane] < 0 || !left) anyp[lane] = pos;
            if (h >= 0 && (valp[lane] < 0 || !left)) { valp[lane] = pos; valh[lane] = h; }
        }
    }
    (void)x1;
    for (int lane = 0; lane < 4; lane++) {
        if (valp[lane] >= 0) { e->pos[lane] = valp[lane]; e->h[lane] = valh[lane]; }
        else if (anyp[lane] >= 0) { e->pos[lane] = anyp[lane]; e->h[lane] = 0x8000; }
        else { e->pos[lane] = -1; e->h[lane] = 0; }
    }
}

/* ---------------- blending ---------------- */

static inline int s16(uint32_t v) { return (int16_t)(v & 0xFFFF); }
static inline uint32_t u16(uint32_t v) { return v & 0xFFFF; }
/* new a = (b * 256 + (a - b) * (w + 1)) >> 8, w taken as a signed halfword, byte 1 of the result */
static inline uint8_t mix(int a, int b, uint32_t w)
{
    const int32_t v = b * 256 + (a - b) * s16(w) + (a - b);
    return (uint8_t)(v >> 8);
}
/* the left half's per-pixel weights from start: a first quad of h + sa/2 + lane * sa
 * (unclamped), then + 4 sa per quad, clamped at 0xFFFF */
static void left_acc(uint32_t h, uint32_t sa, int signed_lane, int nq, int64_t* acc)
{
    const int64_t r59 = h ? (int64_t)h + (sa >> 1) : 0xFFFF;
    int64_t cur[4];
    for (int l = 0; l < 4; l++) cur[l] = r59 + (int64_t)(signed_lane ? s16(sa) : (int)u16(sa)) * l;
    for (int q = 0; q < nq; q++) {
        if (q) for (int l = 0; l < 4; l++) { const int64_t v = cur[l] + 4 * (int64_t)sa; cur[l] = v > 0xFFFF ? 0xFFFF : v; }
        for (int l = 0; l < 4; l++) acc[4 * q + l] = cur[l];
    }
}
/* 8000: a Z shape on lines ya (blended from x0 to the middle) and yb (after it) */
static void apply_z(lines_t* L, int ya, int yb, int x0, int x1, uint32_t h2, uint32_t h3, int64_t* acc)
{
    const int n = x1 - x0 + 1, mid = (x0 + x1) >> 1;
    const uint32_t sa = h2 ? slope(h2, n) : 0, sb = h3 ? slope(h3, n) : 0;
    const int start = h2 ? x0 : mid, end = h3 ? x1 : mid, nq = (end - start + 4) >> 2;
    left_acc(h2, sa, 0, nq, acc);
    const uint32_t r58 = h3 ? sb : 0xFFFF;
    for (int i = 0; i < 4 * nq; i++) {
        const int p = start + i;
        if (p >= L->len) break;
        uint32_t wl = (p > mid || p > end) ? 0xFFFF : (uint32_t)acc[i];
        wl >>= 8;
        const uint32_t r41 = u16((uint32_t)(p - mid - 1)) * u16(sb) + r58;
        uint32_t wr = (p <= mid || p > end) ? 0xFFFF : 0xFFFFu - r41;
        wr >>= 8;
        uint8_t* a = PX(L, ya, p); uint8_t* b = PX(L, yb, p);
        for (int c = 1; c < 4; c++) { const int va = a[c], vb = b[c]; a[c] = mix(va, vb, wl); b[c] = mix(vb, va, wr); }
    }
}
/* 8288: a U shape, only line ya blended */
static void apply_u(lines_t* L, int ya, int yb, int x0, int x1, uint32_t h2, uint32_t h3, int64_t* acc)
{
    const int n = x1 - x0 + 1, mid = (x0 + x1) >> 1;
    const uint32_t sa = h2 ? slope(h2, n) : 0, sb = h3 ? slope(h3, n) : 0;
    const int start = h2 ? x0 : mid, end = h3 ? x1 : mid, nq = (end - start + 4) >> 2;
    left_acc(h2, sa, 1, nq, acc);
    const uint32_t r21 = h3 ? 0xFFFFu - sb : 0xFFFF, nsb = 0u - sb;
    for (int i = 0; i < 4 * nq; i++) {
        const int p = start + i;
        if (p >= L->len) break;
        uint32_t w = p > mid ? (uint32_t)(s16((uint32_t)(p - mid - 1)) * s16(nsb) + (int32_t)r21) : (uint32_t)acc[i];
        if (p > end) w = 0xFFFF;
        w >>= 8;
        uint8_t* a = PX(L, ya, p); const uint8_t* b = PX(L, yb, p);
        for (int c = 1; c < 4; c++) a[c] = mix(a[c], b[c], w);
    }
}
/* 9930: a run of one pixel */
static void apply_one(lines_t* L, int y, int x)
{
    uint8_t* a = PX(L, y, x); uint8_t* b = PX(L, y + 1, x);
    for (int c = 1; c < 4; c++) { const int va = a[c], vb = b[c]; a[c] = (uint8_t)((224 * va + 32 * vb) >> 8); b[c] = (uint8_t)((224 * vb + 32 * va) >> 8); }
}

/* ---------------- a pair of lines ---------------- */

typedef struct { int x0, x1; uint32_t h2, h3; int top; } rec_t;

/* everything the pair (y, y+1) does, in order */
static void do_pair(lines_t* L, int y, entry_t* ents, rec_t* zs, rec_t* us, int64_t* acc)
{
    const int len = L->len;
    int ne = 0, nz = 0, nu = 0;
    /* runs: edge flag towards the next line, one polarity */
    int runs[2 * 1300], nr = 0;
    for (int x = 0; x < len;) {
        const int a = AL(L, y, x);
        if (!(a & F_EDGE)) { x++; continue; }
        const int s = x, pol = a & F_POL;
        x++;
        while (x < len && (AL(L, y, x) & F_EDGE) && (AL(L, y, x) & F_POL) == pol) x++;
        runs[2 * nr] = s; runs[2 * nr + 1] = x - s; nr++;
    }
    for (int i = 0; i < nr; i++) if (runs[2 * i + 1] == 1) apply_one(L, y, runs[2 * i]);
    /* entries: runs with corners (A060), then the others longer than 2 (8630) */
    for (int i = 0; i < nr; i++) if (runs[2 * i + 1] > 1 && entry_corners(L, y, runs[2 * i], runs[2 * i + 1], &ents[ne])) ne++;
    for (int i = 0; i < nr; i++) {
        const int x0 = runs[2 * i], n = runs[2 * i + 1], x1 = x0 + n - 1, xl = x0 > 0 ? x0 - 1 : 0;
        if (n <= 2) continue;
        const int cl = (AL(L, y, xl) & F_CROSS) || (AL(L, y + 1, xl) & F_CROSS);
        const int cr = (AL(L, y, x1) & F_CROSS) || (AL(L, y + 1, x1) & F_CROSS);
        if (cl && cr) continue;
        entry_crossings(L, y, x0, n, &ents[ne++]);
    }
    /* 84C0: Z shapes (B-start/A-end, then A-start/B-end), or, without them, U shapes */
    for (int i = 0; i < ne; i++) {
        const int* p = ents[i].pos; const int* h = ents[i].h;
        const int z1 = p[0] >= 0 && p[3] >= 0 && p[0] <= p[3];
        const int z2 = p[2] >= 0 && p[1] >= 0 && p[2] <= p[1];
        const int ua = p[0] >= 0 && p[1] >= 0 && p[0] <= p[1];
        const int ub = p[2] >= 0 && p[3] >= 0 && p[2] <= p[3];
        if (z2) zs[nz++] = (rec_t){ p[2], p[1], (uint32_t)h[2], (uint32_t)h[1], 0 };
        if (z1) zs[nz++] = (rec_t){ p[0], p[3], (uint32_t)h[0], (uint32_t)h[3], 1 };
        if (!z1 && !z2) {
            if (ub) us[nu++] = (rec_t){ p[2], p[3], (uint32_t)h[2], (uint32_t)h[3], 0 };
            if (ua) us[nu++] = (rec_t){ p[0], p[1], (uint32_t)h[0], (uint32_t)h[1], 1 };
        }
    }
    for (int i = 0; i < nz; i++) apply_z(L, zs[i].top ? y : y + 1, zs[i].top ? y + 1 : y, zs[i].x0, zs[i].x1, zs[i].h2, zs[i].h3, acc);
    for (int i = 0; i < nu; i++) apply_u(L, us[i].top ? y : y + 1, us[i].top ? y + 1 : y, us[i].x0, us[i].x1, us[i].h2, us[i].h3, acc);
}

/* ---------------- flags (78A0) ---------------- */

/* the flags of one pixel against its right (r) and lower (d) neighbours,
 * image roles: b7 edge right, b6 edge down, b5/b4 brighter than right/down,
 * low nibble (R+G+B)*1365 >> 16 */
static uint8_t flag(const uint8_t* p, const uint8_t* r, const uint8_t* d)
{
    int s = p[1] + p[2] + p[3], f = (s * 1365) >> 16, er = 0, ed = 0;
    for (int c = 1; c < 4; c++) { er |= abs(p[c] - r[c]) >= 18; if (d) ed |= abs(p[c] - d[c]) >= 18; }
    f |= er << 7 | (s > r[1] + r[2] + r[3]) << 5;
    if (d) f |= ed << 6 | (s > d[1] + d[2] + d[3]) << 4;
    return (uint8_t)f;
}
/* column x's flags (image roles) from colour rows: right neighbour column xr */
static void flags_col(const uint8_t* img, int x, int xr, uint8_t* out /* MLAA_H */)
{
    for (int y = 0; y < MLAA_H; y++) {
        const uint8_t* p = img + ((long)y * MLAA_W + x) * 4;
        const uint8_t* r = img + ((long)y * MLAA_W + xr) * 4;
        const uint8_t* d = y + 1 < MLAA_H ? p + MLAA_W * 4 : NULL;   /* the last row has no edge down */
        uint8_t f = flag(p, r, d);
        if (y >= MLAA_H - 4) f = (f & 0xF0) | 2;                    /* the last quad's luma (78A0 epilogue) */
        out[y] = f;
    }
}
static inline uint8_t swapbits(uint8_t a) { return (uint8_t)((a & 0x0F) | ((a >> 1) & 0x50) | ((a << 1) & 0xA0)); }

/* ---------------- passes ---------------- */

/* one pass over lines in jobs of `per`; img_lines describes the whole image;
 * al: the pass's flags for nal lines */
static void run_jobs(lines_t* img, int per, uint8_t* al, int nal, void (*job_flags)(lines_t*, int, uint8_t*, void*), void* ctx)
{
    const int nl = img->nlines, len = img->len;
    entry_t* ents = malloc(sizeof(entry_t) * 1400);
    rec_t* zs = malloc(sizeof(rec_t) * 2800); rec_t* us = malloc(sizeof(rec_t) * 2800);
    int64_t* acc = malloc(sizeof(int64_t) * (len + 16));
    uint8_t* loc = malloc((size_t)(per + 8) * len * 4);
    for (int j = 0; j * per < nl; j++) {
        const int r0 = j * per, r1 = r0 + per - 1 < nl - 1 ? r0 + per - 1 : nl - 1, lo = r0 > 0 ? r0 - 1 : 0;
        if (job_flags) job_flags(img, j, al, ctx);
        const int wrap = (job_flags && r1 == nl - 1) ? 3 : 0;      /* the column pass's last job: lines 1, 2, 3 follow */
        const int last = wrap ? nl - 1 : (r1 < nl - 2 ? r1 : nl - 2), hi = last + 1;
        /* the job's own copy of lines lo..hi */
        lines_t L = { loc, (long)len * 4, 4, hi - lo + 1, len, al + (long)lo * len, nal - lo };
        for (int l = lo; l <= hi; l++) {
            const int src = l < nl ? l : l - nl + 1;
            for (int p = 0; p < len; p++) memcpy(loc + ((long)(l - lo) * len + p) * 4, PX(img, src, p), 4);
        }
        for (int y = lo; y <= last; y++) do_pair(&L, y - lo, ents, zs, us, acc);
        for (int l = r0; l <= r1; l++)
            for (int p = 0; p < len; p++) memcpy(PX(img, l, p), loc + ((long)(l - lo) * len + p) * 4, 4);
    }
    free(ents); free(zs); free(us); free(acc); free(loc);
}

typedef struct { uint8_t* argb; uint8_t* al0; uint8_t* fl; } col_ctx;
/* a column job computes its flags from memory as it finds it: line r0-1 was
 * blended by the job before; past the last column come columns 1, 2, 3 */
static void col_job_flags(lines_t* img, int j, uint8_t* al, void* vctx)
{
    col_ctx* c = vctx;
    (void)img;
    uint8_t col[MLAA_H];
    memcpy(al, c->al0, (size_t)MLAA_W * MLAA_H);
    if (j > 0) {
        const int x = 256 * j - 1;
        flags_col(c->argb, x, x + 1, col);
        for (int y = 0; y < MLAA_H; y++) al[(long)x * MLAA_H + y] = swapbits(col[y]);
    }
    if (j == 4) {
        flags_col(c->argb, MLAA_W - 1, 1, col);
        for (int y = 0; y < MLAA_H; y++) {
            al[(long)(MLAA_W - 1) * MLAA_H + y] = swapbits(col[y]);
            c->fl[(long)y * MLAA_W + MLAA_W - 1] = col[y];          /* what the job leaves in alpha */
        }
        for (int k = 0; k < 3; k++) {                                   /* lines 1280.. = columns 1.. */
            flags_col(c->argb, 1 + k, 2 + k, col);
            for (int y = 0; y < MLAA_H; y++) al[(long)(MLAA_W + k) * MLAA_H + y] = swapbits(col[y]);
        }
    }
}

void mlaa_ref(uint8_t* argb)
{
    const int W = MLAA_W, H = MLAA_H;
    /* the column pass: flags of every column from the image as it comes */
    uint8_t* al0 = malloc((size_t)W * H);        /* column-major: line x, position y */
    uint8_t* alc = malloc((size_t)(W + 4) * H);
    uint8_t* fl = malloc((size_t)W * H);         /* image roles, row-major, for the alpha left behind */
    uint8_t col[MLAA_H];
    for (int x = 0; x < W; x++) {
        flags_col(argb, x, (x + 1) % W, col);
        for (int y = 0; y < H; y++) { al0[(long)x * H + y] = swapbits(col[y]); fl[(long)y * W + x] = col[y]; }
    }
    memset(alc, 0, (size_t)(W + 4) * H);
    col_ctx cc = { argb, al0, fl };
    lines_t cols = { argb, 4, (long)W * 4, W, H, NULL, 0 };
    run_jobs(&cols, 256, alc, W + 3, col_job_flags, &cc);
    /* the row pass reads the flags from alpha */
    for (long i = 0; i < (long)W * H; i++) argb[4 * i] = fl[i];
    uint8_t* alr = malloc((size_t)W * (H + 2));
    memset(alr, 0, (size_t)W * (H + 2));
    for (long i = 0; i < (long)W * H; i++) alr[i] = argb[4 * i];
    lines_t rows = { argb, (long)W * 4, 4, H, W, NULL, 0 };
    run_jobs(&rows, 144, alr, H + 2, NULL, NULL);
    free(al0); free(alc); free(fl); free(alr);
}
