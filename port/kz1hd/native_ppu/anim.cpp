/*
 * Animation track sampling, native. See native_ppu.h.
 *
 * func_001879F0(track r3, time f1, out r5): the rotation of a track at a time.
 * The track holds a count at +4 and its keys at [+0xC], 32 bytes each: the
 * key's time at +0, its quaternion (x, y, z, w) at +0x10. Before the first
 * key or after the last it copies that key; in between it blends the two keys
 * around the time -- linearly when they are close (|dot| at least table[3]),
 * otherwise with the engine's polynomial slerp (the table at 0x1879A8).
 * Every step rounds to single exactly where the guest's fmadds/fmuls do.
 */
#include "native_ppu.h"

namespace {

double tab[18];   // the guest's constant table at 0x1879A8 (read-only data)

void load_tab()
{
    static bool done;
    if (done) return;
    for (int i = 0; i < 18; i++) tab[i] = ldf(0x1879A8u + 4u * i);
    done = true;
}

inline void copy_quat(uint32_t out, uint32_t key)
{
    for (uint32_t o = 0; o < 16; o += 4) stf(out + o, ldf(key + 0x10 + o));
}

void sample(uint32_t track, double t, uint32_t out)
{
    const int32_t n = (int32_t)vm_read32(track + 4);
    if (n == 0) return;
    const uint32_t keys = vm_read32(track + 0xC);
    double kt = ldf(keys);
    if (t < kt || t == kt) { copy_quat(out, keys); return; }
    const uint32_t last = keys + ((uint32_t)(n - 1) << 5);
    kt = ldf(last);
    if (t > kt || t == kt) { copy_quat(out, last); return; }
    if (!(n > 1)) return;
    int32_t i = 1;
    uint32_t p = keys + 0x20;
    while (!(t < ldf(p))) {
        i++; p += 0x20;
        if (!(i < n)) return;
    }
    const uint32_t prev = keys + ((uint32_t)(i - 1) << 5), next = keys + ((uint32_t)i << 5);
    copy_quat(out, prev);
    load_tab();
    const double ox = ldf(out), oy = ldf(out + 4), oz = ldf(out + 8), ow = ldf(out + 12);
    const double nx = ldf(next + 0x10), ny = ldf(next + 0x14), nz = ldf(next + 0x18), nw = ldf(next + 0x1C);
    const double pt = ldf(prev);
    double u = S(t - pt);
    u = S(u / S(ldf(next) - pt));
    double d = S(oy * ny);
    d = S(fma(ox, nx, d));
    d = S(fma(oz, nz, d));
    const double dot = S(fma(ow, nw, d)), ndot = S(-fma(ow, nw, d));
    const bool pos = dot - tab[1] >= 0.0;
    const double c = pos ? dot : ndot, sign = pos ? tab[0] : tab[2];
    double w0 = S(tab[0] - u), w1 = u;
    if (c < tab[3]) {
        const double th = S(S(fma(c, S(fma(c, S(fma(c, tab[4], tab[5])), tab[6])), tab[7])) * S(sqrt(S(tab[0] - c))));
        const double inv = S(tab[0] / th), th2 = S(th * th);
        w0 = S(w0 * th);
        w1 = S(u * th);
        const double a2 = S(w0 * w0), b2 = S(w1 * w1);
        double s = S(fma(tab[8], th2, tab[9]));
        s = S(fma(s, th2, tab[10]));
        s = S(fma(s, th2, tab[11]));
        s = S(fma(s, th2, tab[12]));
        const double rs = S(fma(th, s, inv));
        double pa = S(fma(tab[13], a2, tab[14])), pb = S(fma(tab[13], b2, tab[14]));
        pa = S(fma(pa, a2, tab[15])); pb = S(fma(pb, b2, tab[15]));
        pa = S(fma(pa, a2, tab[16])); pb = S(fma(pb, b2, tab[16]));
        pa = S(fma(pa, a2, tab[17])); pb = S(fma(pb, b2, tab[17]));
        pa = S(fma(pa, a2, tab[0]));  pb = S(fma(pb, b2, tab[0]));
        w0 = S(S(w0 * pa) * rs);
        w1 = S(S(w1 * pb) * rs);
    }
    const double k = S(w1 * sign);
    stf(out + 0x0, S(fma(w0, ox, S(k * nx))));
    stf(out + 0x4, S(fma(w0, oy, S(k * ldf(next + 0x14)))));
    stf(out + 0x8, S(fma(w0, oz, S(k * ldf(next + 0x18)))));
    stf(out + 0xC, S(fma(w0, ow, S(k * ldf(next + 0x1C)))));
}

}  // namespace

void func_001879F0(ppu_context* ctx)
{
    const uint32_t track = (uint32_t)ctx->gpr[3], out = (uint32_t)ctx->gpr[5];
    if (__builtin_expect(kz_native_verify, 0)) {
        uint8_t before[16], mine[16];
        memcpy(before, (vm_base + out), 16);
        sample(track, ctx->fpr[1], out);
        memcpy(mine, (vm_base + out), 16);
        memcpy((vm_base + out), before, 16);
        func_001879F0_lifted(ctx);
        kz_native_check("func_001879F0", mine, (vm_base + out), 16);
        return;
    }
    sample(track, ctx->fpr[1], out);
}
