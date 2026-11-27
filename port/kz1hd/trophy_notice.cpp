// The trophy notice on screen and its sound (docs/dev/trophies.md, stage 2). ENHANCEMENT: the PS3 game draws no
// notice of its own (the console's system software did); this card is the port's own design ("Project Vekta" style:
// the site's steel palette, cut corners, Rajdhani), with no console art, cup or phrase.
//
// kz_notice_add (the frame loop, trophies.cpp) reads the trophy's icon from the player's TRP and queues the card.
// The Vulkan backend's overlay hook (ps3recomp patch 2097) runs on the presenting thread once per present: the queue
// steps (trophy_notice.h), each card is drawn on the CPU once at the window's scale (premultiplied alpha: the panel
// at 90-94 %, its border, words and icon solid, the cut corners clear) and blended over the window's image. Under every upscaler and while the game loads, plays a movie or is
// paused: the window presents at all of those times. When a card appears, its sound is added into the game's mix on
// the mixing thread (ps3recomp patch 3039), without a lock.
//
// The sound is Project Vekta's own, made for the project by its author (assets/trophy.mp3, NOTICE.md).
#include "trophies.h"
#include "trophy_list.h"
#include "trophy_notice.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rsx_vulkan_backend.h"
#include "sceNpTrophy.h"
#include "stb_image.h"
#include "stb_truetype.h"
#include "minimp3.h"   // the implementation is native/flashmp3.c's

extern "C" {
typedef void (*ps3_audio_host_mix_fn)(float* mix, uint32_t frames, uint32_t channels);
void ps3_audio_set_host_mix(ps3_audio_host_mix_fn fn);   // ps3recomp cellAudio.c (patch 3039)
}

static_assert(tl::kPlatinum == SCE_NP_TROPHY_GRADE_PLATINUM && tl::kGold == SCE_NP_TROPHY_GRADE_GOLD &&
              tl::kSilver == SCE_NP_TROPHY_GRADE_SILVER && tl::kBronze == SCE_NP_TROPHY_GRADE_BRONZE, "grades");

namespace {

const unsigned char kBold[] = {
#embed "assets/Rajdhani-Bold.ttf"
};
const unsigned char kMedium[] = {
#embed "assets/Rajdhani-Medium.ttf"
};
const unsigned char kSound[] = {
#embed "assets/trophy.mp3"
};

struct Rgb { float r, g, b; };
// "#rrggbb" (strings, not 0x numbers: tools/edition_map.py reads those as EBOOT addresses)
constexpr int hexd(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
constexpr float hexb(const char* s) { return (hexd(s[0]) * 16 + hexd(s[1])) / 255.0f; }
constexpr Rgb hex(const char* s) { return { hexb(s + 1), hexb(s + 3), hexb(s + 5) }; }

const Rgb kPanelA = hex("#11161c"), kPanelB = hex("#0a0d10"), kLine = hex("#25313c"), kSteelHi = hex("#a9d0ee"),
          kSteel = hex("#6ea8d8"), kTrack = hex("#1d2731"), kDim = hex("#9ba7b3"), kWhite = hex("#ffffff");

Rgb grade_colour(int g)
{
    return g == SCE_NP_TROPHY_GRADE_PLATINUM ? hex("#bcd8f0") : g == SCE_NP_TROPHY_GRADE_GOLD ? hex("#e3bd47")
         : g == SCE_NP_TROPHY_GRADE_SILVER ? hex("#cfd6dd") : hex("#c98a4b");
}

// ---- the card on the CPU --------------------------------------------------------------------------------------

// premultiplied colour (px) and alpha (al); blend puts c at coverage a over what is there
struct Canvas {
    int w, h;
    std::vector<Rgb> px;
    std::vector<float> al;
    void blend(int x, int y, Rgb c, float a)
    {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        const size_t i = (size_t)y * w + x;
        Rgb& d = px[i];
        a = std::min(a, 1.0f);
        d = { c.r * a + d.r * (1 - a), c.g * a + d.g * (1 - a), c.b * a + d.b * (1 - a) };
        al[i] = a + al[i] * (1 - a);
    }
};

// coverage of pixel (x, y) by inside(fx, fy), 4x4 samples
template <class F> float cover(int x, int y, F&& inside)
{
    int n = 0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) n += inside(x + (i + 0.5f) / 4, y + (j + 0.5f) / 4);
    return n / 16.0f;
}

struct Font {
    stbtt_fontinfo f;
    bool ok = false;
    explicit Font(const unsigned char* d) { ok = stbtt_InitFont(&f, d, stbtt_GetFontOffsetForIndex(d, 0)) != 0; }
    float scale(float px) const { return stbtt_ScaleForMappingEmToPixels(&f, px); }
    void vmetrics(float px, float* asc, float* desc, float* gap) const
    {
        int a, d, g;
        stbtt_GetFontVMetrics(&f, &a, &d, &g);
        const float s = scale(px);
        *asc = a * s; *desc = d * s; *gap = g * s;
    }
};
const Font& bold() { static const Font f(kBold); return f; }
const Font& medium() { static const Font f(kMedium); return f; }

std::vector<int> utf8(const std::string& s)
{
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        int cp = c, n = 0;
        if (c >= 0xF0) cp = c & 7, n = 3;
        else if (c >= 0xE0) cp = c & 15, n = 2;
        else if (c >= 0xC0) cp = c & 31, n = 1;
        i++;
        for (; n && i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80; n--, i++) cp = cp << 6 | (s[i] & 63);
        out.push_back(n ? 0xFFFD : cp);
    }
    return out;
}

// Width of cps in Font at px with spacing after every letter (CSS letter-spacing).
float measure(const Font& fn, float size, float spacing, const std::vector<int>& cps)
{
    const float s = fn.scale(size);
    float x = 0;
    for (size_t i = 0; i < cps.size(); i++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&fn.f, cps[i], &adv, &lsb);
        x += adv * s + spacing;
        if (i + 1 < cps.size()) x += stbtt_GetCodepointKernAdvance(&fn.f, cps[i], cps[i + 1]) * s;
    }
    return x;
}

float draw_text(Canvas& cv, const Font& fn, float size, float spacing, float x, float base, const std::vector<int>& cps,
                Rgb c)
{
    const float s = fn.scale(size);
    std::vector<unsigned char> bm;
    for (size_t i = 0; i < cps.size(); i++) {
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&fn.f, cps[i], &adv, &lsb);
        const float fx = std::floor(x), sub = x - fx;
        stbtt_GetCodepointBitmapBoxSubpixel(&fn.f, cps[i], s, s, sub, 0, &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            bm.assign((size_t)gw * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(&fn.f, bm.data(), gw, gh, gw, s, s, sub, 0, cps[i]);
            const int ox = (int)fx + x0, oy = (int)std::lround(base) + y0;
            for (int y = 0; y < gh; y++)
                for (int k = 0; k < gw; k++) cv.blend(ox + k, oy + y, c, bm[(size_t)y * gw + k] / 255.0f);
        }
        x += adv * s + spacing;
        if (i + 1 < cps.size()) x += stbtt_GetCodepointKernAdvance(&fn.f, cps[i], cps[i + 1]) * s;
    }
    return x;
}

bool has_glyph(const Font& fn, int cp) { return stbtt_FindGlyphIndex(&fn.f, cp) != 0; }

struct Card {
    int id = 0, grade = 0, got = 0, total = 0;
    std::string name;
    std::vector<unsigned char> icon;   // RGBA
    int iw = 0, ih = 0;
    std::vector<uint8_t> pixels;       // the drawn card, in the window's byte order
    int pw = 0, ph = 0, bgra = -1;
};

void draw(Card& c, int w, int h, double s, int bgra)
{
    Canvas cv{ w, h, std::vector<Rgb>((size_t)w * h, Rgb{ 0, 0, 0 }), std::vector<float>((size_t)w * h, 0.0f) };
    const float cut = (float)tn::px(tn::kCut, s), b = (float)std::max(1, tn::px(1, s));
    const float fw = (float)w, fh = (float)h;
    auto outer = [&](float x, float y) { return x + y >= cut && (fw - x) + (fh - y) >= cut; };
    auto inner = [&](float x, float y) {
        const float d = cut + b * 1.41421356f;
        return x >= b && y >= b && x <= fw - b && y <= fh - b && x + y >= d && (fw - x) + (fh - y) >= d;
    };
    // the panel: a 100-degree gradient from 94 % to 90 % alpha, faint scanlines every 3 pixels from the bottom, the
    // solid 1-pixel border; outside the cut corners nothing.
    const float dx = std::sin(100 * 3.14159265f / 180), dy = -std::cos(100 * 3.14159265f / 180);
    const float len = std::fabs(fw * dx) + std::fabs(fh * dy);
    const int period = std::max(3, tn::px(3, s)), line = std::max(1, tn::px(1, s));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const float co = cover(x, y, outer), ci = cover(x, y, inner);
            const float t = std::clamp(0.5f + ((x + 0.5f - fw / 2) * dx + (y + 0.5f - fh / 2) * dy) / len, 0.0f, 1.0f);
            Rgb p = { kPanelA.r + (kPanelB.r - kPanelA.r) * t, kPanelA.g + (kPanelB.g - kPanelA.g) * t,
                      kPanelA.b + (kPanelB.b - kPanelA.b) * t };
            float pa = 0.94f + (0.90f - 0.94f) * t;
            p = { p.r * pa, p.g * pa, p.b * pa };   // premultiplied
            if ((h - 1 - y) % period < line) {      // a white line at 1.8 % over the panel
                constexpr float k = 0.018f;
                p = { k + p.r * (1 - k), k + p.g * (1 - k), k + p.b * (1 - k) };
                pa = k + pa * (1 - k);
            }
            const size_t i = (size_t)y * w + x;
            cv.px[i] = { p.r * ci + kLine.r * (co - ci), p.g * ci + kLine.g * (co - ci), p.b * ci + kLine.b * (co - ci) };
            cv.al[i] = pa * ci + (co - ci);
        }
    const Rgb gc = grade_colour(c.grade);

    // the icon: the trophy's own picture from the player's TRP, 68 px, corners cut 8 px
    const int is = tn::px(68, s), ix = tn::px(18, s), iy = (h - is) / 2;
    const float icut = (float)tn::px(8, s), fis = (float)is;
    auto icon_in = [&](float x, float y) { return x + y >= icut && (fis - x) + (fis - y) >= icut; };
    for (int y = 0; y < is; y++)
        for (int x = 0; x < is; x++) {
            const float a = cover(x, y, icon_in);
            if (a <= 0) continue;
            if (c.icon.empty()) { cv.blend(ix + x, iy + y, kTrack, a); continue; }
            // bilinear from the icon's size
            const float u = std::clamp((x + 0.5f) * c.iw / fis - 0.5f, 0.0f, c.iw - 1.0f);
            const float v = std::clamp((y + 0.5f) * c.ih / fis - 0.5f, 0.0f, c.ih - 1.0f);
            const int u0 = (int)u, v0 = (int)v, u1 = std::min(u0 + 1, c.iw - 1), v1 = std::min(v0 + 1, c.ih - 1);
            const float fu = u - u0, fv = v - v0;
            float rgba[4];
            for (int k = 0; k < 4; k++) {
                auto at = [&](int uu, int vv) { return c.icon[((size_t)vv * c.iw + uu) * 4 + k] / 255.0f; };
                rgba[k] = (at(u0, v0) * (1 - fu) + at(u1, v0) * fu) * (1 - fv) + (at(u0, v1) * (1 - fu) + at(u1, v1) * fu) * fv;
            }
            cv.blend(ix + x, iy + y, kTrack, a);   // under a transparent icon: the track colour
            cv.blend(ix + x, iy + y, { rgba[0], rgba[1], rgba[2] }, a * rgba[3]);
        }

    // the three lines, centred in the card: kind and grade (15 px), name (26 px, line-height 1.1), progress (14 px)
    const Font& fb = bold();
    const Font& fm = medium();
    if (!fb.ok || !fm.ok) return;
    float a15, d15, g15, a26, d26, g26, a14, d14, g14;
    const float s15 = (float)(15 * s), s26 = (float)(26 * s), s14 = (float)(14 * s);
    fb.vmetrics(s15, &a15, &d15, &g15);
    fb.vmetrics(s26, &a26, &d26, &g26);
    fm.vmetrics(s14, &a14, &d14, &g14);
    const float chip_h = (float)(20 * s), h1 = std::max(chip_h, a15 - d15 + g15), h2 = (float)(26 * 1.1 * s),
                h3 = a14 - d14 + g14, gap = (float)s;
    const float top1 = (h - (h1 + gap + h2 + gap + h3)) / 2, top2 = top1 + h1 + gap, top3 = top2 + h2 + gap;
    auto base = [](float top, float lh, float asc, float desc) { return top + (lh - (asc - desc)) / 2 + asc; };
    const float tx = (float)(102 * s), tr = (float)(w - 20 * s);

    // line 1: the hex chip (the project's own glyph), then "TROPHY · BRONZE"
    const float cx = tx, cy = top1 + (h1 - chip_h) / 2, cs = (float)s;
    static const float hexo[6][2] = { { 9, 0 }, { 18, 5 }, { 18, 15 }, { 9, 20 }, { 0, 15 }, { 0, 5 } };
    static const float hexi[6][2] = { { 9, 4.5f }, { 14, 7.3f }, { 14, 12.7f }, { 9, 15.5f }, { 4, 12.7f }, { 4, 7.3f } };
    auto in_hex = [&](const float (*p)[2], float x, float y) {   // convex, clockwise in y-down coordinates
        for (int k = 0; k < 6; k++) {
            const float* a = p[k];
            const float* bb = p[(k + 1) % 6];
            if ((bb[0] - a[0]) * (y - a[1]) - (bb[1] - a[1]) * (x - a[0]) < 0) return false;
        }
        return true;
    };
    auto seg_dist = [](float px_, float py, const float* a, const float* bb) {
        const float vx = bb[0] - a[0], vy = bb[1] - a[1];
        const float t = std::clamp(((px_ - a[0]) * vx + (py - a[1]) * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
        return std::hypot(px_ - a[0] - t * vx, py - a[1] - t * vy);
    };
    for (int y = (int)cy; y <= (int)(cy + 20 * cs) + 1; y++)
        for (int x = (int)cx; x <= (int)(cx + 18 * cs) + 1; x++) {
            cv.blend(x, y, gc, cover(x, y, [&](float fx, float fy) { return in_hex(hexo, (fx - cx) / cs, (fy - cy) / cs); }));
            cv.blend(x, y, { 0, 0, 0 }, 0.4f * cover(x, y, [&](float fx, float fy) {
                const float u = (fx - cx) / cs, v = (fy - cy) / cs;
                for (int k = 0; k < 6; k++)
                    if (seg_dist(u, v, hexi[k], hexi[(k + 1) % 6]) <= 0.7f) return true;
                return false;
            }));
        }
    const tl::Words& wd = tl::words();
    const float ls1 = (float)(2.4 * s), b1 = base(top1, h1, a15, d15);
    float x = draw_text(cv, fb, s15, ls1, cx + 18 * cs + (float)(8 * s), b1, utf8(wd.trophy),
                        kSteelHi);
    const std::string dot = has_glyph(fb, 0xB7) ? "\xC2\xB7 " : "- ";
    draw_text(cv, fb, s15, ls1, x + (float)(8 * s), b1, utf8(dot + wd.grade[tl::grade_index(c.grade)]), gc);

    // line 2: the name, with an ellipsis when it is too long
    const float ls2 = (float)(0.4 * s);
    std::vector<int> name = utf8(c.name);
    const std::vector<int> ell = has_glyph(fb, 0x2026) ? std::vector<int>{ 0x2026 } : std::vector<int>{ '.', '.', '.' };
    if (measure(fb, s26, ls2, name) > tr - tx) {
        while (!name.empty()) {
            std::vector<int> t = name;
            t.insert(t.end(), ell.begin(), ell.end());
            if (measure(fb, s26, ls2, t) <= tr - tx) break;
            name.pop_back();
        }
        while (!name.empty() && name.back() == ' ') name.pop_back();
        name.insert(name.end(), ell.begin(), ell.end());
    }
    draw_text(cv, fb, s26, ls2, tx, base(top2, h2, a26, d26), name, kWhite);

    // line 3: "34 / 59" and a 3 px bar
    char count[32];
    std::snprintf(count, sizeof count, "%d / %d", c.got, c.total);
    x = draw_text(cv, fm, s14, (float)s, tx, base(top3, h3, a14, d14), utf8(count), kDim);
    const int bx0 = (int)std::lround(x + 10 * s), bx1 = (int)std::lround(tr), bh = std::max(1, tn::px(3, s));
    const int by = (int)std::lround(top3 + (h3 - bh) / 2);
    const int fill = c.total > 0 ? bx0 + (int)std::lround((bx1 - bx0) * std::clamp((double)c.got / c.total, 0.0, 1.0)) : bx0;
    for (int y = by; y < by + bh; y++)
        for (int xx = bx0; xx < bx1; xx++) cv.blend(xx, y, xx < fill ? kSteel : kTrack, 1);

    c.pixels.resize((size_t)w * h * 4);
    for (size_t i = 0; i < cv.px.size(); i++) {
        const uint8_t r = (uint8_t)std::lround(std::clamp(cv.px[i].r, 0.0f, 1.0f) * 255);
        const uint8_t g = (uint8_t)std::lround(std::clamp(cv.px[i].g, 0.0f, 1.0f) * 255);
        const uint8_t bl = (uint8_t)std::lround(std::clamp(cv.px[i].b, 0.0f, 1.0f) * 255);
        uint8_t* d = &c.pixels[i * 4];
        d[0] = bgra ? bl : r; d[1] = g; d[2] = bgra ? r : bl;
        d[3] = (uint8_t)std::lround(std::clamp(cv.al[i], 0.0f, 1.0f) * 255);
    }
    c.pw = w; c.ph = h; c.bgra = bgra;
}

// ---- the queue and the hooks ----------------------------------------------------------------------------------

struct Item { std::shared_ptr<Card> c; };
std::mutex s_mu;
tn::Queue<Item> s_q;
std::vector<uint8_t> s_out;   // the cards' pixels for the present that asked (valid until the next call)
std::atomic<bool> s_notices{ true }, s_sound{ true };

std::vector<float> s_pcm;     // the sound, stereo 48 kHz
std::atomic<int> s_pos{ -1 }; // the next frame to mix, -1 idle
// ASSUMPTION: half amplitude (-6 dB) sits under the game's own effects; the port does not read the game's volumes.
constexpr float kGain = 0.5f;

void mix(float* m, uint32_t frames, uint32_t ch)
{
    int p = s_pos.load(std::memory_order_acquire);
    if (p < 0 || ch < 2) return;
    const int start = p, total = (int)(s_pcm.size() / 2);
    for (uint32_t i = 0; i < frames && p < total; i++, p++) {
        m[i * ch] += s_pcm[(size_t)p * 2] * kGain;
        m[i * ch + 1] += s_pcm[(size_t)p * 2 + 1] * kGain;
    }
    int want = start;   // a new notice may have restarted it meanwhile: keep that
    s_pos.compare_exchange_strong(want, p >= total ? -1 : p, std::memory_order_acq_rel);
}

void decode_sound()
{
    mp3dec_t d;
    mp3dec_init(&d);
    mp3dec_frame_info_t info;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    for (size_t off = 0; off < sizeof kSound;) {
        const int n = mp3dec_decode_frame(&d, kSound + off, (int)(sizeof kSound - off), pcm, &info);
        if (!info.frame_bytes) break;
        off += (size_t)info.frame_bytes;
        if (info.hz != 48000 || info.channels < 1) continue;
        for (int i = 0; i < n; i++)
            for (int k = 0; k < 2; k++) s_pcm.push_back(pcm[i * info.channels + (info.channels > 1 ? k : 0)] / 32768.0f);
    }
    std::fprintf(stderr, "[trophy] notice sound: %.2f s\n", s_pcm.size() / 2 / 48000.0);
}

double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

int overlay(uint32_t ww, uint32_t wh, int bgra, const void** pixels, size_t* bytes, rsx_overlay_rect* r, int max)
{
    std::lock_guard<std::mutex> g(s_mu);
    const double t = now_ms();
    if (!s_q.n) return 0;
    tn::Placed p[tn::kShown];
    const int np = s_q.step(t, ww, wh, p, [](Item& it) {
        const bool snd = s_sound.load() && !s_pcm.empty();
        if (snd) s_pos.store(0, std::memory_order_release);
        std::fprintf(stderr, "[trophy] notice shown %d %s%s\n", it.c->id, it.c->name.c_str(), snd ? ", sound played" : "");
    });
    const double s = tn::scale_for(wh);
    s_out.clear();
    int n = 0;
    for (int k = 0; k < np; k++) {
        Card& c = *s_q.item(p[k].slot).c;
        if (c.pw != p[k].w || c.ph != p[k].h || c.bgra != bgra) draw(c, p[k].w, p[k].h, s, bgra);
        if (c.pixels.empty()) continue;
        const uint32_t off = (uint32_t)s_out.size();
        s_out.insert(s_out.end(), c.pixels.begin(), c.pixels.end());
        if (n < max) r[n++] = { p[k].x, p[k].y, (uint32_t)c.pw, (uint32_t)c.ph, off, (uint32_t)c.pw };
    }
    *pixels = s_out.data();
    *bytes = s_out.size();
    return n;
}

}  // namespace

extern "C" void kz_notice_init(void)
{
    decode_sound();
    rsx_vulkan_set_overlay(overlay);
    ps3_audio_set_host_mix(mix);
}

extern "C" void kz_notice_options(int notices, int sound)
{
    s_notices = notices != 0;
    s_sound = sound != 0;
}

extern "C" void kz_notice_add(int id, int grade, const char* name, int got, int total)
{
    if (!s_notices) return;
    auto c = std::make_shared<Card>();
    c->id = id; c->grade = grade; c->name = name ? name : ""; c->got = got; c->total = total;
    size_t size = 0;
    if (void* png = ps3_trophy_icon(id, &size)) {
        int w, h, n;
        if (unsigned char* rgba = stbi_load_from_memory((const unsigned char*)png, (int)size, &w, &h, &n, 4)) {
            c->icon.assign(rgba, rgba + (size_t)w * h * 4);
            c->iw = w; c->ih = h;
            stbi_image_free(rgba);
        }
        std::free(png);
    }
    std::lock_guard<std::mutex> g(s_mu);
    if (!s_q.push(Item{ c })) std::fprintf(stderr, "[trophy] notice queue full: %d not shown\n", id);
}
