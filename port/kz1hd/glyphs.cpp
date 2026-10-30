/*
 * Killzone 1 HD -- button prompts that follow the device in use.
 *
 * The menus draw buttons from two textures, both DXT5:
 *   - the image-map font (menu/shared/typefaces/imagemaps/imagemapsmall, and
 *     its _bw twin with the same pixels): 1024x128, five levels. The hints
 *     under every menu ("<-> Left/Right  O Back") and dialog buttons are
 *     characters of this font. Its glyph table gives each glyph's rectangle:
 *     cross, triangle, square, circle, left, right, up, down, L1, L2, L3, R1,
 *     R2, R3, up-down, then text-entry marks, then left-right.
 *   - menu/pause/textures/buttons: 512x32, three levels, a row of 20-texel
 *     cells (square, cross, circle, triangle, left, right, up, down, L2, L1,
 *     R2, R1, R3, L3), for "<buttonimage name=Cross>" in text. The tutorial
 *     hints' "<buttonimage function=reload>" go through the same tag handler
 *     (FUN_002399C0 resolves the function to a button name), so they follow too.
 * and Pause > Controls draws the pad itself from menu/pause/textures/controller_image
 * (512x512, seven levels): a DualShock 3 outline whose labels the screen places
 * around it (Move and Look under the sticks). For an Xbox or Nintendo pad its
 * face buttons, L and R are relettered in place; for the keyboard it becomes a
 * keyboard (W A S D where the left stick was) and a mouse under Look.
 *
 * The draw engine hands every texture to rsx_texture_seen_hook as it comes
 * into use. The game's copies are recognised by a hash of their top level and
 * kept (in memory, never on disk), and from then on rewritten with the set for
 * the device the player last touched (pad_active_device): the original for a
 * PlayStation pad, Xbox letters and bumpers for an Xbox or generic pad (8BitDo
 * in X-input), Nintendo's by position for a Switch layout, and keycaps or a
 * mouse for the keyboard -- the first input kz1hd_keys.ini binds to that
 * button. Each replacement glyph is drawn here at four times the size into the
 * box the original glyph covers, filtered down, and DXT5-encoded; blocks no
 * glyph touches keep the game's bytes.
 *
 * KZ_GLYPHS=0 leaves the textures alone; KZ_GLYPHS_LOG=1 logs what is seen;
 * KZ_GLYPHS_DUMP=<dir> writes each set's top level (raw DXT5) when built.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <strings.h>

extern "C" {
extern void (*rsx_texture_seen_hook)(uint8_t* bytes, uint32_t fmt, uint32_t w, uint32_t h, uint32_t span);
int pad_active_device(void);
}

namespace {

enum { DEV_KEYBOARD, DEV_XBOX, DEV_PLAYSTATION, DEV_NINTENDO, DEV_COUNT };
enum Btn { B_SQUARE, B_CROSS, B_CIRCLE, B_TRIANGLE, B_LEFT, B_RIGHT, B_UP, B_DOWN,
           B_L1, B_L2, B_L3, B_R1, B_R2, B_R3, B_UPDOWN, B_LEFTRIGHT };
static const char* const kBtnName[] = {
    "SQUARE", "CROSS", "CIRCLE", "TRIANGLE", "LEFT", "RIGHT", "UP", "DOWN",
    "L1", "L2", "L3", "R1", "R2", "R3", "", "" };

struct Cell { int x, y, w, h; Btn b; };
struct Atlas {
    const char* name;
    int w, h, levels;
    uint64_t hash;          /* FNV-1a of the original's top level: recognises it, keeps none of it */
    bool strip;             /* the small strip's style: pale pills; else the font's dark ones */
    std::vector<Cell> cells;   /* none: the pad picture (build_picture) */
};

const Atlas kAtlases[] = {
    { "imagemap", 1024, 128, 5, 0x567ba1b0e79d51b1ull, false, {
        {   1,  0, 50, 56, B_CROSS }, {  53,  0, 50, 56, B_TRIANGLE }, { 108,  0, 48, 56, B_SQUARE },
        { 160,  0, 48, 56, B_CIRCLE }, { 210,  0, 30, 56, B_LEFT }, { 240,  0, 30, 56, B_RIGHT },
        { 273,  0, 48, 56, B_UP }, { 323,  0, 42, 56, B_DOWN }, { 378,  0, 53, 56, B_L1 },
        { 443,  0, 53, 56, B_L2 }, { 505,  0, 63, 56, B_L3 }, { 568,  0, 60, 56, B_R1 },
        {   1, 56, 60, 56, B_R2 }, {  65, 56, 60, 56, B_R3 }, { 128, 56, 48, 56, B_UPDOWN },
        { 278, 56, 23, 56, B_LEFTRIGHT } } },
    { "buttons", 512, 32, 3, 0x0416705a676f27f2ull, true, {
        {   0, 0, 20, 18, B_SQUARE }, {  20, 0, 20, 18, B_CROSS }, {  40, 0, 20, 18, B_CIRCLE },
        {  60, 0, 20, 18, B_TRIANGLE }, {  80, 0, 20, 18, B_LEFT }, { 100, 0, 20, 18, B_RIGHT },
        { 120, 0, 20, 18, B_UP }, { 140, 0, 20, 18, B_DOWN }, { 160, 0, 20, 18, B_L2 },
        { 180, 0, 20, 18, B_L1 }, { 200, 0, 20, 18, B_R2 }, { 220, 0, 20, 18, B_R1 },
        { 240, 0, 20, 18, B_R3 }, { 260, 0, 20, 18, B_L3 } } },
    { "controller", 512, 512, 7, 0x0866fbe50e30fb12ull, false, {} },
};
const int kNumAtlases = sizeof kAtlases / sizeof kAtlases[0];

int span_of(const Atlas& a)
{
    int n = 0;
    for (int l = 0, w = a.w, h = a.h; l < a.levels; l++, w = std::max(w / 2, 1), h = std::max(h / 2, 1))
        n += std::max(w / 4, 1) * std::max(h / 4, 1) * 16;
    return n;
}

uint64_t fnv(const uint8_t* p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

/* ---- DXT5 ------------------------------------------------------------------ */

struct Px { uint8_t r, g, b, a; };

Px ex565(uint16_t c)
{
    return Px{ (uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31), 255 };
}
void palette4(uint16_t c0, uint16_t c1, Px pal[4])
{
    const Px e0 = ex565(c0), e1 = ex565(c1);
    pal[0] = e0; pal[1] = e1;
    pal[2] = Px{ (uint8_t)((2 * e0.r + e1.r) / 3), (uint8_t)((2 * e0.g + e1.g) / 3), (uint8_t)((2 * e0.b + e1.b) / 3), 255 };
    pal[3] = Px{ (uint8_t)((e0.r + 2 * e1.r) / 3), (uint8_t)((e0.g + 2 * e1.g) / 3), (uint8_t)((e0.b + 2 * e1.b) / 3), 255 };
}

void dxt5_decode(const uint8_t* src, int w, int h, std::vector<Px>& img)
{
    img.assign((size_t)w * h, Px{0, 0, 0, 0});
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++, src += 16) {
            int ap[8] = { src[0], src[1] };
            if (ap[0] > ap[1]) for (int k = 1; k < 7; k++) ap[k + 1] = ((7 - k) * ap[0] + k * ap[1]) / 7;
            else { for (int k = 1; k < 5; k++) ap[k + 1] = ((5 - k) * ap[0] + k * ap[1]) / 5; ap[6] = 0; ap[7] = 255; }
            uint64_t ai = 0;
            for (int i = 0; i < 6; i++) ai |= (uint64_t)src[2 + i] << (8 * i);
            Px pal[4];
            palette4((uint16_t)(src[8] | src[9] << 8), (uint16_t)(src[10] | src[11] << 8), pal);
            const uint32_t ci = (uint32_t)(src[12] | src[13] << 8 | src[14] << 16 | (uint32_t)src[15] << 24);
            for (int i = 0; i < 16; i++) {
                Px p = pal[(ci >> (2 * i)) & 3];
                p.a = (uint8_t)ap[(ai >> (3 * i)) & 7];
                img[(size_t)(by * 4 + i / 4) * w + bx * 4 + i % 4] = p;
            }
        }
}

void dxt5_block(const Px* p /*16*/, uint8_t* out)
{
    uint8_t amin = 255, amax = 0;
    for (int i = 0; i < 16; i++) { amin = std::min(amin, p[i].a); amax = std::max(amax, p[i].a); }
    out[0] = amax; out[1] = amin;
    uint64_t ai = 0;
    if (amax != amin) {
        int pal[8] = { amax, amin };
        for (int k = 1; k < 7; k++) pal[k + 1] = ((7 - k) * amax + k * amin) / 7;
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 8; k++) { int d = std::abs(pal[k] - p[i].a); if (d < bd) { bd = d; best = k; } }
            ai |= (uint64_t)best << (3 * i);
        }
    }
    for (int i = 0; i < 6; i++) out[2 + i] = (uint8_t)(ai >> (8 * i));

    /* colour: the two extremes, by luminance, of the visible texels */
    int lo = -1, hi = -1, llo = 1 << 30, lhi = -1;
    for (int i = 0; i < 16; i++) {
        if (p[i].a < 8) continue;
        int l = p[i].r * 3 + p[i].g * 6 + p[i].b;
        if (l < llo) { llo = l; lo = i; }
        if (l > lhi) { lhi = l; hi = i; }
    }
    auto to565 = [](const Px& c) { return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); };
    uint16_t c0 = hi >= 0 ? to565(p[hi]) : 0, c1 = lo >= 0 ? to565(p[lo]) : 0;
    if (c0 < c1) std::swap(c0, c1);
    uint32_t ci = 0;
    if (c0 != c1) {
        Px pal[4];
        palette4(c0, c1, pal);
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 4; k++) {
                int dr = pal[k].r - p[i].r, dg = pal[k].g - p[i].g, db = pal[k].b - p[i].b;
                int d = dr * dr + dg * dg + db * db;
                if (d < bd) { bd = d; best = k; }
            }
            ci |= (uint32_t)best << (2 * i);
        }
    }
    out[8] = (uint8_t)c0; out[9] = (uint8_t)(c0 >> 8);
    out[10] = (uint8_t)c1; out[11] = (uint8_t)(c1 >> 8);
    for (int i = 0; i < 4; i++) out[12 + i] = (uint8_t)(ci >> (8 * i));
}

/* ---- drawing, at 4x into one glyph's box ------------------------------------ */

const int S = 4;

struct RGBA { float r, g, b, a; };
struct Canvas {   /* premultiplied */
    int w, h;
    std::vector<RGBA> px;
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, RGBA{0, 0, 0, 0}) {}
    template <class F> void fill(RGBA c, F inside) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (inside(x + 0.5f, y + 0.5f)) {
                    RGBA& d = px[(size_t)y * w + x];
                    d.r = c.r * c.a + d.r * (1 - c.a); d.g = c.g * c.a + d.g * (1 - c.a);
                    d.b = c.b * c.a + d.b * (1 - c.a); d.a = c.a + d.a * (1 - c.a);
                }
    }
};

bool in_rrect(float x, float y, float x0, float y0, float x1, float y1, float r)
{
    float cx = x < x0 + r ? x0 + r : x > x1 - r ? x1 - r : x;
    float cy = y < y0 + r ? y0 + r : y > y1 - r ? y1 - r : y;
    return x >= x0 && x <= x1 && y >= y0 && y <= y1 && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}
bool in_tri(float x, float y, float ax, float ay, float bx, float by, float qx, float qy)
{
    float d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by);
    float d2 = (x - qx) * (by - qy) - (bx - qx) * (y - qy);
    float d3 = (x - ax) * (qy - ay) - (qx - ax) * (y - ay);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

/* 5x7 capitals, digits and a few marks; rows top-down, bit 4 = left. */
const unsigned char* glyph5x7(char ch)
{
    static const struct { char c; unsigned char r[7]; } f[] = {
        {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},
        {'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
        {'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},
        {'J',{7,2,2,2,2,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}},{'N',{17,17,25,21,19,17,17}},{'O',{14,17,17,17,17,17,14}},
        {'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
        {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},
        {'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
        {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},{'0',{14,17,19,21,25,17,14}},
        {'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},{'3',{31,2,4,2,1,17,14}},
        {'4',{2,6,10,18,31,2,2}},{'5',{31,16,30,1,1,17,14}},{'6',{6,8,16,30,17,17,14}},
        {'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,2,12}},
        {'-',{0,0,0,31,0,0,0}},{'+',{0,4,4,31,4,4,0}},{'.',{0,0,0,0,0,12,12}},
        {',',{0,0,0,0,12,4,8}},{'/',{1,1,2,4,8,16,16}},{'=',{0,0,31,0,31,0,0}},
        {'[',{14,8,8,8,8,8,14}},{']',{14,2,2,2,2,2,14}},{';',{0,12,12,0,12,4,8}},
        {'\'',{4,4,8,0,0,0,0}},{'`',{8,4,2,0,0,0,0}},{'\\',{16,16,8,4,2,1,1}},
    };
    for (auto& g : f) if (g.c == ch) return g.r;
    return nullptr;
}

/* Text centred on (cx, cy), `h` tall; `bold` widens each stroke. */
void text(Canvas& cv, float cx, float cy, float h, const std::string& s, struct RGBA c, bool bold = false)
{
    const float px = h / 7.0f, adv = 6 * px;
    const float wtot = s.size() * adv - px, x0 = cx - wtot / 2, y0 = cy - h / 2;
    cv.fill(c, [&](float x, float y) {
        int i = (int)std::floor((x - x0) / adv);
        if (i < 0 || i >= (int)s.size()) return false;
        const unsigned char* g = glyph5x7((char)std::toupper((unsigned char)s[i]));
        if (!g) return false;
        float ly = (y - y0) / px;
        if (ly < 0 || ly >= 7) return false;
        float lx = (x - x0 - i * adv) / px;
        for (float o : { 0.0f, bold ? 0.45f : 0.0f, bold ? -0.45f : 0.0f }) {
            int ix = (int)std::floor(lx + o);
            if (ix >= 0 && ix < 5 && (g[(int)ly] >> (4 - ix) & 1)) return true;
        }
        return false;
    });
}

const RGBA kWhite{0.93f, 0.93f, 0.93f, 1}, kDark{0.10f, 0.10f, 0.11f, 0.92f}, kPale{0.80f, 0.80f, 0.80f, 1};
const RGBA kInk{0.12f, 0.12f, 0.13f, 1}, kHot{0.95f, 0.55f, 0.15f, 1};

/* Everything below draws into a glyph box of the canvas. */
struct Box {
    float x0, y0, x1, y1;
    float w() const { return x1 - x0; } float h() const { return y1 - y0; }
    float cx() const { return (x0 + x1) / 2; } float cy() const { return (y0 + y1) / 2; }
};

void face_button(Canvas& cv, Box b, const char* letter, RGBA col)
{
    const float r = std::min(b.w(), b.h()) / 2, t = r * 0.13f;
    cv.fill(kDark, [&](float x, float y) { return std::hypot(x - b.cx(), y - b.cy()) <= r; });
    cv.fill(kWhite, [&](float x, float y) { float d = std::hypot(x - b.cx(), y - b.cy()); return d <= r && d >= r - t; });
    text(cv, b.cx(), b.cy(), r * 1.05f, letter, col, true);
}
void pill(Canvas& cv, Box b, const std::string& s, bool pale)
{
    const float r = b.h() * 0.32f, t = b.h() * 0.07f;
    cv.fill(pale ? kPale : kDark, [&](float x, float y) { return in_rrect(x, y, b.x0, b.y0, b.x1, b.y1, r); });
    if (!pale) cv.fill(kWhite, [&](float x, float y) {
        return in_rrect(x, y, b.x0, b.y0, b.x1, b.y1, r) && !in_rrect(x, y, b.x0 + t, b.y0 + t, b.x1 - t, b.y1 - t, r - t); });
    const float th = std::min(b.h() * 0.58f, b.w() * 0.8f / (s.size() * 6.0f) * 7.0f);
    text(cv, b.cx(), b.cy(), th, s, pale ? kInk : kWhite, true);
}
void stick(Canvas& cv, Box b, const char* s, bool pale)
{
    const float r = std::min(b.w(), b.h()) / 2;
    cv.fill(pale ? kPale : kDark, [&](float x, float y) { return std::hypot(x - b.cx(), y - b.cy()) <= r; });
    if (!pale) cv.fill(kWhite, [&](float x, float y) { float d = std::hypot(x - b.cx(), y - b.cy()); return d <= r && d >= r * 0.87f; });
    text(cv, b.cx(), b.cy(), r * 0.75f, s, pale ? kInk : kWhite, true);
}
void arrow(Canvas& cv, float cx, float cy, float a, int dir, RGBA c)   /* 0 left 1 right 2 up 3 down */
{
    cv.fill(c, [&](float x, float y) {
        switch (dir) {
        case 0:  return in_tri(x, y, cx - a, cy, cx + a * 0.6f, cy - a, cx + a * 0.6f, cy + a);
        case 1:  return in_tri(x, y, cx + a, cy, cx - a * 0.6f, cy - a, cx - a * 0.6f, cy + a);
        case 2:  return in_tri(x, y, cx, cy - a, cx - a, cy + a * 0.6f, cx + a, cy + a * 0.6f);
        default: return in_tri(x, y, cx, cy + a, cx - a, cy - a * 0.6f, cx + a, cy - a * 0.6f);
        }
    });
}
void cap(Canvas& cv, Box b)   /* an empty keycap */
{
    const float m = std::min(b.w(), b.h()), r = m * 0.2f, t = m * 0.08f;
    cv.fill(kDark, [&](float x, float y) { return in_rrect(x, y, b.x0, b.y0, b.x1, b.y1, r); });
    cv.fill(kWhite, [&](float x, float y) {
        return in_rrect(x, y, b.x0, b.y0, b.x1, b.y1, r) && !in_rrect(x, y, b.x0 + t, b.y0 + t, b.x1 - t, b.y1 - t, r - t); });
}

std::string key_label(std::string k)
{
    for (auto& ch : k) ch = (char)std::toupper((unsigned char)ch);
    static const struct { const char* n; const char* l; } m[] = {
        {"SPACE","SPC"},{"RETURN","ENT"},{"ENTER","ENT"},{"ESCAPE","ESC"},{"BACKSPACE","BKS"},
        {"TAB","TAB"},{"CTRL","CTL"},{"LEFT CTRL","CTL"},{"RIGHT CTRL","CTL"},{"SHIFT","SHF"},
        {"LEFT SHIFT","SHF"},{"RIGHT SHIFT","SHF"},{"ALT","ALT"},{"LEFT ALT","ALT"},{"RIGHT ALT","ALT"},
        {"CAPSLOCK","CAP"},{"DELETE","DEL"},{"INSERT","INS"},{"HOME","HOM"},{"END","END"},
        {"PAGEUP","PGU"},{"PAGEDOWN","PGD"},
    };
    for (auto& e : m) if (k == e.n) return e.l;
    if (k.size() > 3) k.resize(3);
    return k;
}

/* which: 0 left, 1 right, 2 middle, 3 wheel up, 4 wheel down, 5 side */
void mouse(Canvas& cv, Box b, int which)
{
    const float hh = b.h(), ww = std::min(b.w(), hh * 0.68f);
    const float x0 = b.cx() - ww / 2, x1 = b.cx() + ww / 2, y0 = b.y0, y1 = b.y1, r = ww * 0.48f;
    const float mid = b.cx(), split = y0 + hh * 0.42f, t = hh * 0.055f;
    auto body = [&](float x, float y) { return in_rrect(x, y, x0, y0, x1, y1, r); };
    cv.fill(kDark, body);
    if (which == 0) cv.fill(kHot, [&](float x, float y) { return x < mid && y < split && body(x, y); });
    if (which == 1) cv.fill(kHot, [&](float x, float y) { return x > mid && y < split && body(x, y); });
    cv.fill(kWhite, [&](float x, float y) { return body(x, y) && !in_rrect(x, y, x0 + t, y0 + t, x1 - t, y1 - t, r - t); });
    cv.fill(kWhite, [&](float x, float y) { return std::fabs(x - mid) < t / 2 && y < split && body(x, y); });
    cv.fill(kWhite, [&](float x, float y) { return std::fabs(y - split) < t / 2 && body(x, y); });
    if (which >= 2 && which <= 4)
        cv.fill(kHot, [&](float x, float y) { return in_rrect(x, y, mid - ww * 0.12f, y0 + hh * 0.11f, mid + ww * 0.12f, y0 + hh * 0.33f, ww * 0.1f); });
    if (which == 3) arrow(cv, mid, y0 + hh * 0.66f, hh * 0.14f, 2, kHot);
    if (which == 4) arrow(cv, mid, y0 + hh * 0.70f, hh * 0.14f, 3, kHot);
    if (which == 5) text(cv, mid, y0 + hh * 0.68f, hh * 0.25f, "4", kHot, true);
}

void keycap(Canvas& cv, Box b, const std::string& key)
{
    cap(cv, b);
    std::string up = key; for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
    const float m = std::min(b.w(), b.h());
    if (up == "UP" || up == "DOWN" || up == "LEFT" || up == "RIGHT") {
        arrow(cv, b.cx(), b.cy(), m * 0.25f, up == "LEFT" ? 0 : up == "RIGHT" ? 1 : up == "UP" ? 2 : 3, kWhite);
        return;
    }
    const std::string l = key_label(key);
    const float th = std::min(m * (l.size() == 1 ? 0.55f : 0.42f), (b.w() * 0.8f) / (l.size() * 6.0f) * 7.0f);
    text(cv, b.cx(), b.cy(), th, l, kWhite, l.size() == 1);
}

/* The first input kz1hd_keys.ini binds to a pad button. */
std::string first_binding(const char* button)
{
    const char* path = getenv("PAD_KEYMAP");
    FILE* f = fopen(path ? path : "kz1hd_keys.ini", "r");
    if (!f) return "";
    char line[256];
    std::string out;
    while (out.empty() && fgets(line, sizeof line, f)) {
        if (char* h = strchr(line, '#')) *h = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char t[64];
        if (sscanf(line, " %63s", t) != 1 || strcasecmp(t, button)) continue;
        std::string v = eq + 1;
        v = v.substr(0, v.find(','));
        size_t a = v.find_first_not_of(" \t\r\n"), z = v.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) out = v.substr(a, z - a + 1);
    }
    fclose(f);
    return out;
}

/* Draws button `btn` for `dev` into box `b`; false = keep the original. */
bool draw_button(Canvas& cv, Box b, Btn btn, int dev, bool pale)
{
    const RGBA green{0.40f, 0.85f, 0.30f, 1}, red{0.95f, 0.30f, 0.25f, 1},
               blue{0.30f, 0.60f, 1.00f, 1}, yellow{1.00f, 0.82f, 0.25f, 1};
    if (dev == DEV_XBOX || dev == DEV_NINTENDO) {
        const bool x = dev == DEV_XBOX;
        switch (btn) {   /* Nintendo by position: the bottom button is B */
        case B_SQUARE:   face_button(cv, b, x ? "X" : "Y", x ? blue : kWhite); return true;
        case B_CROSS:    face_button(cv, b, x ? "A" : "B", x ? green : kWhite); return true;
        case B_CIRCLE:   face_button(cv, b, x ? "B" : "A", x ? red : kWhite); return true;
        case B_TRIANGLE: face_button(cv, b, x ? "Y" : "X", x ? yellow : kWhite); return true;
        case B_L1: pill(cv, b, x ? "LB" : "L", pale); return true;
        case B_L2: pill(cv, b, x ? "LT" : "ZL", pale); return true;
        case B_R1: pill(cv, b, x ? "RB" : "R", pale); return true;
        case B_R2: pill(cv, b, x ? "RT" : "ZR", pale); return true;
        case B_L3: stick(cv, b, "LS", pale); return true;
        case B_R3: stick(cv, b, "RS", pale); return true;
        default: return false;   /* the d-pad arrows are anyone's */
        }
    }
    /* keyboard and mouse */
    switch (btn) {
    case B_LEFT:  keycap(cv, b, "Left"); return true;
    case B_RIGHT: keycap(cv, b, "Right"); return true;
    case B_UP:    keycap(cv, b, "Up"); return true;
    case B_DOWN:  keycap(cv, b, "Down"); return true;
    case B_UPDOWN: case B_LEFTRIGHT: {   /* one key, both arrows */
        cap(cv, b);
        const float a = std::min(b.w(), b.h()) * 0.2f, g = a * 1.25f;
        if (btn == B_UPDOWN) { arrow(cv, b.cx(), b.cy() - g, a, 2, kWhite); arrow(cv, b.cx(), b.cy() + g, a, 3, kWhite); }
        else { arrow(cv, b.cx() - g, b.cy(), a, 0, kWhite); arrow(cv, b.cx() + g, b.cy(), a, 1, kWhite); }
        return true;
    }
    default: break;
    }
    const std::string k = first_binding(kBtnName[btn]);
    if (k.empty()) return false;
    std::string up = k; for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
    if (up == "MOUSELEFT") mouse(cv, b, 0);
    else if (up == "MOUSERIGHT") mouse(cv, b, 1);
    else if (up == "MOUSEMIDDLE") mouse(cv, b, 2);
    else if (up == "WHEELUP") mouse(cv, b, 3);
    else if (up == "WHEELDOWN") mouse(cv, b, 4);
    else if (up == "MOUSE4" || up == "MOUSE5") mouse(cv, b, 5);
    else keycap(cv, b, k);
    return true;
}

/* ---- the pad picture ------------------------------------------------------- */

/* Composites a canvas drawn at S times the texels into img at (ox, oy). */
void put(std::vector<Px>& img, int iw, int ih, const Canvas& cv, int ox, int oy)
{
    for (int y = 0; y < cv.h / S; y++)
        for (int x = 0; x < cv.w / S; x++) {
            const int tx = ox + x, ty = oy + y;
            if (tx < 0 || ty < 0 || tx >= iw || ty >= ih) continue;
            RGBA s{0, 0, 0, 0};
            for (int yy = 0; yy < S; yy++)
                for (int xx = 0; xx < S; xx++) {
                    const RGBA& q = cv.px[(size_t)(y * S + yy) * cv.w + x * S + xx];
                    s.r += q.r; s.g += q.g; s.b += q.b; s.a += q.a;
                }
            const float n = (float)(S * S), a = s.a / n;
            if (a <= 0) continue;
            Px& d = img[(size_t)ty * iw + tx];
            const float da = d.a / 255.0f, oa = a + da * (1 - a);
            auto ch = [&](float src_pm, uint8_t dst) {
                return (uint8_t)std::min(255.0f, std::lround((src_pm / n * 255 + dst * da * (1 - a)) / std::max(oa, 1e-4f)) * 1.0f); };
            d.r = ch(s.r, d.r); d.g = ch(s.g, d.g); d.b = ch(s.b, d.b);
            d.a = (uint8_t)std::lround(oa * 255);
        }
}

/* The picture's own style: light lines on a translucent dark body. */
const RGBA kLine{0.80f, 0.80f, 0.80f, 1}, kBody{0.05f, 0.055f, 0.05f, 0.33f};

void stroke_rrect(Canvas& cv, float x0, float y0, float x1, float y1, float r, float t, RGBA fill)
{
    cv.fill(fill, [&](float x, float y) { return in_rrect(x, y, x0, y0, x1, y1, r); });
    cv.fill(kLine, [&](float x, float y) {
        return in_rrect(x, y, x0, y0, x1, y1, r) && !in_rrect(x, y, x0 + t, y0 + t, x1 - t, y1 - t, std::max(r - t, 0.0f)); });
}

/* A key at (x, y, w, h) texels, with its label; `hot` marks the movement keys. */
void pic_key(std::vector<Px>& img, int iw, int ih, int x, int y, int w, int h, const std::string& l, bool hot)
{
    Canvas cv(w * S, h * S);
    const float t = 2.0f * S;
    stroke_rrect(cv, 0, 0, (float)w * S, (float)h * S, 5.0f * S, t, kBody);
    if (hot) cv.fill(RGBA{kHot.r, kHot.g, kHot.b, 0.35f}, [&](float px, float py) {
        return in_rrect(px, py, t, t, w * S - t, h * S - t, 3.0f * S); });
    const float th = std::min(h * S * 0.42f, (w * S * 0.8f) / (l.size() * 6.0f) * 7.0f);
    text(cv, w * S / 2.0f, h * S / 2.0f, th, l, kLine, true);
    put(img, iw, ih, cv, x, y);
}

/* The keyboard: a block of keys around W A S D (centred where the left stick
 * was, over Move) and a mouse at the right stick (over Look). */
void pic_keyboard(std::vector<Px>& img, int iw, int ih)
{
    std::fill(img.begin(), img.end(), Px{0, 0, 0, 0});
    const int k = 34, g = 4, x0 = 40, y0 = 124;
    {   /* the keyboard's outline */
        Canvas cv(168 * S, 176 * S);
        stroke_rrect(cv, 0, 0, 168.0f * S, 176.0f * S, 14.0f * S, 2.5f * S, kBody);
        put(img, iw, ih, cv, x0 - 14, y0 - 14);
    }
    const char* rows[3][3] = { { "Q", "W", "E" }, { "A", "S", "D" }, { "Z", "X", "C" } };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            const std::string l = rows[r][c];
            const bool hot = l == "W" || l == "A" || l == "S" || l == "D";
            pic_key(img, iw, ih, x0 + 14 + c * (k + g) + r * 7, y0 + r * (k + g), k, k, l, hot);
        }
    pic_key(img, iw, ih, x0, y0 + 3 * (k + g), 2 * k, k, "SHIFT", false);
    pic_key(img, iw, ih, x0 + 2 * k + g, y0 + 3 * (k + g), k + 22, k, "CTRL", false);
    {   /* the mouse */
        const int w = 86, h = 134;
        Canvas cv(w * S, h * S);
        const float t = 2.5f * S, W = (float)w * S, H = (float)h * S, r = W * 0.47f, split = H * 0.42f;
        stroke_rrect(cv, 0, 0, W, H, r, t, kBody);
        cv.fill(kLine, [&](float x, float y) { return std::fabs(x - W / 2) < t / 2 && y < split && in_rrect(x, y, 0, 0, W, H, r); });
        cv.fill(kLine, [&](float x, float y) { return std::fabs(y - split) < t / 2 && in_rrect(x, y, 0, 0, W, H, r); });
        cv.fill(kLine, [&](float x, float y) {
            return in_rrect(x, y, W / 2 - W * 0.09f, H * 0.12f, W / 2 + W * 0.09f, H * 0.30f, W * 0.08f) &&
                   !in_rrect(x, y, W / 2 - W * 0.09f + t, H * 0.12f + t, W / 2 + W * 0.09f - t, H * 0.30f - t, W * 0.04f); });
        put(img, iw, ih, cv, 262 - w / 2, 212 - h / 2);
    }
}

/* An Xbox or Nintendo pad over the DualShock outline: the four face buttons
 * (rings at 11 texels around the centres below) and the L/R marks relettered. */
void pic_pad(std::vector<Px>& img, int iw, int ih, int dev)
{
    const bool x = dev == DEV_XBOX;
    const RGBA green{0.40f, 0.85f, 0.30f, 1}, red{0.95f, 0.30f, 0.25f, 1},
               blue{0.30f, 0.60f, 1.00f, 1}, yellow{1.00f, 0.82f, 0.25f, 1};
    const Px fill{13, 14, 13, 85};   /* the body's own texels */
    struct { int cx, cy; const char* l; RGBA c; } faces[4] = {
        { 298, 107, x ? "Y" : "X", x ? yellow : kLine }, { 325, 139, x ? "B" : "A", x ? red : kLine },
        { 296, 172, x ? "A" : "B", x ? green : kLine },  { 269, 139, x ? "X" : "Y", x ? blue : kLine } };
    for (auto& f : faces) {
        for (int y = f.cy - 9; y <= f.cy + 9; y++)
            for (int xx = f.cx - 9; xx <= f.cx + 9; xx++)
                if (std::hypot(xx + 0.5f - f.cx, y + 0.5f - f.cy) <= 8.6f) img[(size_t)y * iw + xx] = fill;
        Canvas cv(20 * S, 20 * S);
        text(cv, 10.0f * S, 10.0f * S, 11.0f * S, f.l, f.c, true);
        put(img, iw, ih, cv, f.cx - 10, f.cy - 10);
    }
    struct { int cx, cy; const char* l; } marks[2] = { { 83, 61, x ? "LB" : "L" }, { 300, 61, x ? "RB" : "R" } };
    for (auto& m : marks) {
        for (int y = m.cy - 6; y <= m.cy + 6; y++)
            for (int xx = m.cx - 9; xx <= m.cx + 9; xx++) img[(size_t)y * iw + xx] = fill;
        Canvas cv(24 * S, 14 * S);
        text(cv, 12.0f * S, 7.0f * S, 9.0f * S, m.l, kLine, true);
        put(img, iw, ih, cv, m.cx - 12, m.cy - 7);
    }
}

/* ---- building a set ------------------------------------------------------- */

/* The original's top level, decoded, with the replaced glyphs drawn over
 * their boxes, then every level re-encoded; level-0 blocks no glyph touches
 * keep the game's bytes. */
std::vector<uint8_t> build_set(const Atlas& a, const std::vector<uint8_t>& orig, int dev)
{
    std::vector<Px> img;
    dxt5_decode(orig.data(), a.w, a.h, img);
    std::vector<char> touched((size_t)(a.w / 4) * (a.h / 4), 0);
    if (a.cells.empty()) {   /* the pad picture: redrawn, every block re-encoded */
        if (dev == DEV_KEYBOARD) pic_keyboard(img, a.w, a.h);
        else pic_pad(img, a.w, a.h, dev);
        std::fill(touched.begin(), touched.end(), 1);
    }

    /* The box each original glyph covers (alpha), and the face buttons' size:
     * round buttons, keys and the mouse take that height, centred where the
     * original sat and at least as wide as it (a wide pill becomes a wide key). */
    auto bbox = [&](const Cell& c, int& x0, int& y0, int& x1, int& y1) {
        x0 = c.x + c.w; y0 = c.y + c.h; x1 = c.x - 1; y1 = c.y - 1;
        for (int y = c.y; y < c.y + c.h; y++)
            for (int x = c.x; x < c.x + c.w; x++)
                if (img[(size_t)y * a.w + x].a > 40) {
                    x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
        return x1 >= x0;
    };
    float std_h = 0, std_cy = 0;
    for (const Cell& c : a.cells)
        if (c.b == B_CROSS) { int x0, y0, x1, y1; if (bbox(c, x0, y0, x1, y1)) { std_h = (float)(y1 + 1 - y0); std_cy = (y0 + y1 + 1) / 2.0f - c.y; } }
    if (a.strip) { std_h = a.cells[0].h - 1.0f; std_cy = a.cells[0].h / 2.0f; }   /* its symbols are thin: use the row */

    for (const Cell& c : a.cells) {
        int bx0, by0, bx1, by1;
        if (!bbox(c, bx0, by0, bx1, by1)) continue;
        Canvas cv(c.w * S, c.h * S);
        Box b{ (bx0 - c.x) * (float)S, (by0 - c.y) * (float)S, (bx1 + 1 - c.x) * (float)S, (by1 + 1 - c.y) * (float)S };
        const bool pill_shape = (c.b == B_L1 || c.b == B_L2 || c.b == B_R1 || c.b == B_R2) && dev != DEV_KEYBOARD;
        if (std_h > 0 && !pill_shape) {
            const float hh = std_h * S, ww = std::min((float)c.w * S, std::max(hh, b.w()));
            b = Box{ b.cx() - ww / 2, std_cy * S - hh / 2, b.cx() + ww / 2, std_cy * S + hh / 2 };
            if (b.x0 < 0) { b.x1 -= b.x0; b.x0 = 0; }
            if (b.x1 > c.w * S) { b.x0 -= b.x1 - c.w * S; b.x1 = (float)c.w * S; }
        }
        if (!draw_button(cv, b, c.b, dev, a.strip)) continue;
        for (int y = 0; y < c.h; y++)
            for (int x = 0; x < c.w; x++) {
                RGBA s{0, 0, 0, 0};
                for (int yy = 0; yy < S; yy++)
                    for (int xx = 0; xx < S; xx++) {
                        const RGBA& q = cv.px[(size_t)(y * S + yy) * cv.w + x * S + xx];
                        s.r += q.r; s.g += q.g; s.b += q.b; s.a += q.a;
                    }
                const float n = (float)(S * S);
                Px& p = img[(size_t)(c.y + y) * a.w + c.x + x];
                p.a = (uint8_t)std::lround(s.a / n * 255);
                const float k = s.a > 1e-4f ? 255.0f / s.a : 0;
                p.r = (uint8_t)std::min(255.0f, s.r * k); p.g = (uint8_t)std::min(255.0f, s.g * k);
                p.b = (uint8_t)std::min(255.0f, s.b * k);
                touched[(size_t)((c.y + y) / 4) * (a.w / 4) + (c.x + x) / 4] = 1;
            }
    }

    std::vector<uint8_t> out(orig.size());
    uint8_t* o = out.data();
    std::vector<Px> lv = img;
    for (int l = 0, w = a.w, h = a.h; l < a.levels; l++) {
        for (int by = 0; by < std::max(h / 4, 1); by++)
            for (int bx = 0; bx < std::max(w / 4, 1); bx++, o += 16) {
                if (l == 0 && !touched[(size_t)by * (w / 4) + bx]) {
                    memcpy(o, orig.data() + (o - out.data()), 16);
                    continue;
                }
                Px blk[16];
                for (int i = 0; i < 16; i++) {
                    int x = std::min(bx * 4 + i % 4, w - 1), y = std::min(by * 4 + i / 4, h - 1);
                    blk[i] = lv[(size_t)y * w + x];
                }
                dxt5_block(blk, o);
            }
        if (l + 1 == a.levels) break;
        const int nw = std::max(w / 2, 1), nh = std::max(h / 2, 1);
        std::vector<Px> nx((size_t)nw * nh);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                float r = 0, g = 0, bb = 0, al = 0;
                for (int k = 0; k < 4; k++) {
                    const Px& q = lv[(size_t)std::min(y * 2 + k / 2, h - 1) * w + std::min(x * 2 + k % 2, w - 1)];
                    r += q.r * q.a; g += q.g * q.a; bb += q.b * q.a; al += q.a;
                }
                Px& p = nx[(size_t)y * nw + x];
                p.a = (uint8_t)std::lround(al / 4);
                p.r = al > 0 ? (uint8_t)(r / al) : 0; p.g = al > 0 ? (uint8_t)(g / al) : 0; p.b = al > 0 ? (uint8_t)(bb / al) : 0;
            }
        lv.swap(nx); w = nw; h = nh;
    }
    return out;
}

/* ---- the game's copies -------------------------------------------------- */

struct State {
    std::vector<uint8_t> original;            /* the game's bytes, once seen */
    std::vector<uint8_t> sets[DEV_COUNT];     /* built on first use */
    std::vector<uint64_t> set_hash;           /* top levels of the sets built */
    std::vector<uint8_t*> copies;             /* where the game keeps it */
};
std::mutex g_mu;
State g_st[kNumAtlases];
int g_shown = -1;
bool g_log;
const char* g_dump;

const std::vector<uint8_t>& set_for(int ai, int dev)
{
    State& st = g_st[ai];
    if (dev == DEV_PLAYSTATION) return st.original;
    if (st.sets[dev].empty()) {
        const Atlas& a = kAtlases[ai];
        st.sets[dev] = build_set(a, st.original, dev);
        st.set_hash.push_back(fnv(st.sets[dev].data(), (size_t)a.w * a.h));
        if (g_dump) {
            std::string p = std::string(g_dump) + "/" + a.name + "_" + std::to_string(dev) + ".dxt5";
            if (FILE* f = fopen(p.c_str(), "wb")) { fwrite(st.sets[dev].data(), 1, (size_t)a.w * a.h, f); fclose(f); }
        }
    }
    return st.sets[dev];
}

bool is_ours(int ai, const uint8_t* p)
{
    const Atlas& a = kAtlases[ai];
    const uint64_t h = fnv(p, (size_t)a.w * a.h);
    if (h == a.hash) return true;
    for (uint64_t s : g_st[ai].set_hash) if (h == s) return true;
    return false;
}

void write_set(int ai, uint8_t* p, int dev)
{
    const std::vector<uint8_t>& s = set_for(ai, dev);
    memcpy(p, s.data(), s.size());
}

void seen(uint8_t* p, uint32_t fmt, uint32_t w, uint32_t h, uint32_t span)
{
    if ((fmt & 0x9F) != 0x88) return;   /* DXT5 */
    for (int ai = 0; ai < kNumAtlases; ai++) {
        const Atlas& a = kAtlases[ai];
        if ((int)w != a.w || (int)h != a.h || (int)span < span_of(a)) continue;
        std::lock_guard<std::mutex> lk(g_mu);
        State& st = g_st[ai];
        if (!is_ours(ai, p)) continue;
        if (st.original.empty()) {   /* first sight: the game's; KZ_GLYPHS_DUMP builds every set */
            st.original.assign(p, p + span_of(a));
            if (g_dump) for (int d = 0; d < DEV_COUNT; d++) set_for(ai, d);
        }
        if (std::find(st.copies.begin(), st.copies.end(), p) == st.copies.end()) st.copies.push_back(p);
        if (g_log) fprintf(stderr, "[kz-glyphs] %s in use (%zu copies)\n", a.name, st.copies.size());
        write_set(ai, p, pad_active_device());
        return;
    }
}

void apply(int dev)
{
    for (int ai = 0; ai < kNumAtlases; ai++) {
        State& st = g_st[ai];
        if (st.original.empty()) continue;
        for (size_t i = 0; i < st.copies.size();) {
            if (!is_ours(ai, st.copies[i])) { st.copies.erase(st.copies.begin() + (long)i); continue; }   /* memory reused */
            write_set(ai, st.copies[i], dev);
            i++;
        }
    }
}

}  // namespace

void kz_glyphs_register(void)
{
    const char* e = getenv("KZ_GLYPHS");
    if (e && *e == '0') return;
    g_log = getenv("KZ_GLYPHS_LOG") != nullptr;
    g_dump = getenv("KZ_GLYPHS_DUMP");
    rsx_texture_seen_hook = seen;
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const int dev = pad_active_device();
            std::lock_guard<std::mutex> lk(g_mu);
            if (dev == g_shown) continue;
            if (g_log) fprintf(stderr, "[kz-glyphs] device %d\n", dev);
            apply(dev);
            g_shown = dev;
        }
    }).detach();
}
