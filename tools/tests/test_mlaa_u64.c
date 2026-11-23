/* The MLAA shader's SPU float emulation (ps3recomp libs/video/vulkan/rsx_vk_mlaa.comp) in its two forms, as C: the
 * first on uint64_t (patches 2036-2086), the second on (low, high) word pairs as the shader now keeps them (patch
 * 2087, #129: AMD's Windows driver ended the process compiling the uint64_t form). Random fmul, ffma (also with the
 * addend's exponent next to the product's, where the terms cancel), fsum with shifted terms and ffi must agree bit
 * for bit. A model: the shader itself is checked on a GPU by tools/mlaa_harness (GPU = C reference). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t uint;
typedef uint64_t u64t;
static int findMSB(uint v) { return v ? 31 - __builtin_clz(v) : -1; }
static int imax(int a, int b) { return a > b ? a : b; }
/* ---- original (uint64) ---- */
static int msb64(u64t v) {
    uint hi = (uint)(v >> 32);
    return hi ? 32 + findMSB(hi) : findMSB((uint)v);
}
static uint fpack(uint s, u64t m, int e) {
    if (!m)
        return s << 31;
    int n = msb64(m);
    uint man = n > 23 ? (uint)(m >> (n - 23)) : (uint)(m << (23 - n));
    int be = e + n + 127;
    if (be <= 0)
        return s << 31;
    if (be >= 255)
        return (s << 31) | 0x7F7FFFFFu;
    return (s << 31) | ((uint)be << 23) | (man & 0x7FFFFFu);
}
static int fsplit(uint f, uint *s, uint *m, int *e) {
    int be = (f >> 23) & 0xFF;
    *s = f >> 31;
    if (!be) {
        *m = 0;
        *e = 0;
        return 0;
    }
    *m = (f & 0x7FFFFFu) | 0x800000u;
    *e = be - 150;
    return 1;
}
static uint fmul(uint a, uint b) {
    uint sa, sb, ma, mb;
    int ea, eb;
    int na = fsplit(a, &sa, &ma, &ea), nb = fsplit(b, &sb, &mb, &eb);
    if (!na || !nb)
        return (sa ^ sb) << 31;
    return fpack(sa ^ sb, (u64t)ma * mb, ea + eb);
}
static uint fsum(uint sp, u64t mp, int ep, uint sc, u64t mc, int ec) {
    if (!mp)
        return mc ? fpack(sc, mc, ec) : ((sp & sc) << 31);
    if (!mc)
        return fpack(sp, mp, ep);
    int tp = ep + msb64(mp), tc = ec + msb64(mc);
    int E = imax(tp, tc) - 62;
    u64t ap, ac;
    int k = ep - E;
    if (k >= 0)
        ap = mp << k;
    else {
        int r = -k;
        ap = r >= 64 ? 1 : ((mp >> r) | (((mp << (64 - r)) != 0) ? 1 : 0));
    }
    k = ec - E;
    if (k >= 0)
        ac = mc << k;
    else {
        int r = -k;
        ac = r >= 64 ? 1 : ((mc >> r) | (((mc << (64 - r)) != 0) ? 1 : 0));
    }
    if (sp == sc)
        return fpack(sp, ap + ac, E);
    if (ap == ac)
        return 0;
    return ap > ac ? fpack(sp, ap - ac, E) : fpack(sc, ac - ap, E);
}
static uint ffma(uint a, uint b, uint c) {
    uint sa, sb, sc, ma, mb, mc;
    int ea, eb, ec;
    int na = fsplit(a, &sa, &ma, &ea), nb = fsplit(b, &sb, &mb, &eb);
    fsplit(c, &sc, &mc, &ec);
    if (!na || !nb)
        return fsum(sa ^ sb, 0, 0, sc, mc, ec);
    return fsum(sa ^ sb, (u64t)ma * mb, ea + eb, sc, mc, ec);
}
static uint ffi(uint a, uint est) {
    int e = (est >> 23) & 0xFF;
    if (!e)
        return 0;
    u64t m = (((u64t)(0x2000u | ((est >> 10) & 0x1FFFu))) << 19) - (u64t)(est & 0x3FFu) * (u64t)(a & 0x7FFFFu);
    if (m < (1ull << 32)) {
        m <<= 1;
        e--;
        if (!e)
            return 0;
    }
    return (est & 0x80000000u) | ((uint)e << 23) | ((uint)(m >> 9) & 0x7FFFFFu);
}
/* ---- uvec2 ---- */
typedef struct {
    uint x, y;
} v2;
static v2 V(uint x, uint y) {
    v2 r = {x, y};
    return r;
}
static v2 u64(uint v) { return V(v, 0); }
static int z(v2 a) { return (a.x | a.y) == 0; }
static int gt(v2 a, v2 b) { return a.y > b.y || (a.y == b.y && a.x > b.x); }
static v2 add(v2 a, v2 b) {
    uint lo = a.x + b.x;
    uint c = lo < a.x;
    return V(lo, a.y + b.y + c);
}
static v2 sub(v2 a, v2 b) {
    uint lo = a.x - b.x;
    uint br = a.x < b.x;
    return V(lo, a.y - b.y - br);
}
static v2 mul(uint a, uint b) {
    u64t p = (u64t)a * b;
    return V((uint)p, (uint)(p >> 32));
}
static v2 shl(v2 a, uint n) {
    if (!n)
        return a;
    if (n >= 32)
        return V(0, a.x << (n - 32));
    return V(a.x << n, (a.y << n) | (a.x >> (32 - n)));
}
static v2 shr(v2 a, uint n) {
    if (!n)
        return a;
    if (n >= 32)
        return V(a.y >> (n - 32), 0);
    return V((a.x >> n) | (a.y << (32 - n)), a.y >> n);
}
static int msb2(v2 v) { return v.y ? 32 + findMSB(v.y) : findMSB(v.x); }
static uint fpack2(uint s, v2 m, int e) {
    if (z(m))
        return s << 31;
    int n = msb2(m);
    uint man = n > 23 ? shr(m, n - 23).x : shl(m, 23 - n).x;
    int be = e + n + 127;
    if (be <= 0)
        return s << 31;
    if (be >= 255)
        return (s << 31) | 0x7F7FFFFFu;
    return (s << 31) | ((uint)be << 23) | (man & 0x7FFFFFu);
}
static uint fmul2(uint a, uint b) {
    uint sa, sb, ma, mb;
    int ea, eb;
    int na = fsplit(a, &sa, &ma, &ea), nb = fsplit(b, &sb, &mb, &eb);
    if (!na || !nb)
        return (sa ^ sb) << 31;
    return fpack2(sa ^ sb, mul(ma, mb), ea + eb);
}
static v2 sticky(v2 m, int r) {
    if (r >= 64)
        return V(1, 0);
    v2 v = shr(m, r);
    if (!z(shl(m, 64 - r)))
        v.x |= 1;
    return v;
}
static uint fsum2(uint sp, v2 mp, int ep, uint sc, v2 mc, int ec) {
    if (z(mp))
        return !z(mc) ? fpack2(sc, mc, ec) : ((sp & sc) << 31);
    if (z(mc))
        return fpack2(sp, mp, ep);
    int tp = ep + msb2(mp), tc = ec + msb2(mc);
    int E = imax(tp, tc) - 62;
    v2 ap, ac;
    int k = ep - E;
    ap = k >= 0 ? shl(mp, k) : sticky(mp, -k);
    k = ec - E;
    ac = k >= 0 ? shl(mc, k) : sticky(mc, -k);
    if (sp == sc)
        return fpack2(sp, add(ap, ac), E);
    if (ap.x == ac.x && ap.y == ac.y)
        return 0;
    return gt(ap, ac) ? fpack2(sp, sub(ap, ac), E) : fpack2(sc, sub(ac, ap), E);
}
static uint ffma2(uint a, uint b, uint c) {
    uint sa, sb, sc, ma, mb, mc;
    int ea, eb, ec;
    int na = fsplit(a, &sa, &ma, &ea), nb = fsplit(b, &sb, &mb, &eb);
    fsplit(c, &sc, &mc, &ec);
    if (!na || !nb)
        return fsum2(sa ^ sb, V(0, 0), 0, sc, u64(mc), ec);
    return fsum2(sa ^ sb, mul(ma, mb), ea + eb, sc, u64(mc), ec);
}
static uint ffi2(uint a, uint est) {
    int e = (est >> 23) & 0xFF;
    if (!e)
        return 0;
    v2 m = sub(shl(u64(0x2000u | ((est >> 10) & 0x1FFFu)), 19), mul(est & 0x3FFu, a & 0x7FFFFu));
    if (m.y == 0) {
        m = shl(m, 1);
        e--;
        if (!e)
            return 0;
    }
    return (est & 0x80000000u) | ((uint)e << 23) | (shr(m, 9).x & 0x7FFFFFu);
}
static uint64_t st = 88172645463325252ull;
static uint rnd(void) {
    st ^= st << 13;
    st ^= st >> 7;
    st ^= st << 17;
    return (uint)st;
}
static uint rf(void) {
    uint r = rnd();
    switch (rnd() & 7) {
    case 0:
        return r & 0x807FFFFFu;
    case 1:
        return (r & 0x80FFFFFFu) | 0x3F000000u;
    case 2:
        return r ^ (rnd() & 0xFF);
    default:
        return r;
    }
}
int main(int argc, char **argv) {
    long bad = 0, n = 0;
    long iters = argc > 1 ? atol(argv[1]) : 3000000;
    for (long i = 0; i < iters; i++) {
        uint a = rf(), b = rf(), c = rf();
        n++;
        if (fmul(a, b) != fmul2(a, b)) {
            if (bad++ < 5)
                printf("fmul %08x %08x\n", a, b);
        }
        if (ffma(a, b, c) != ffma2(a, b, c)) {
            if (bad++ < 5)
                printf("ffma %08x %08x %08x\n", a, b, c);
        }
        /* nearby exponents to hit cancellation */
        uint c2 = (c & 0x807FFFFFu) | ((((a >> 23) & 0xFF) + ((b >> 23) & 0xFF) - 127 + (int)(rnd() % 5) - 2) & 0xFF)
                                          << 23;
        if (ffma(a, b, c2) != ffma2(a, b, c2)) {
            if (bad++ < 5)
                printf("ffma2 %08x %08x %08x\n", a, b, c2);
        }
        if (ffi(a, b) != ffi2(a, b)) {
            if (bad++ < 5)
                printf("ffi %08x %08x\n", a, b);
        }
        int sh = rnd() % 120 - 60;
        v2 x = V(rnd(), rnd() >> (rnd() % 32)), y = V(rnd(), rnd() >> (rnd() % 32));
        if (fsum(a >> 31, ((u64t)x.y << 32) | x.x, sh, b >> 31, ((u64t)y.y << 32) | y.x, -sh) !=
            fsum2(a >> 31, x, sh, b >> 31, y, -sh)) {
            if (bad++ < 5)
                printf("fsum\n");
        }
    }
    printf("test_mlaa_u64: %ld cases, %ld mismatches: %s\n", n, bad, bad ? "FAIL" : "ok");
    return bad != 0;
}
