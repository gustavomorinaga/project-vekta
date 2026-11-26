/*
 * The 2D layer on a widescreen frame, and the HUD's size and position. See
 * native_ppu.h and camera.cpp.
 *
 * With the cameras widened (camera.cpp), the frame is rendered anamorphic and
 * stretched to the window, so anything drawn in the game's 1280x720 screen
 * space would be stretched with it. Each 2D object takes its projection from
 * func_0005C498 (an orthographic one) as it is drawn: (0, 1280, 720, 0) built
 * at 0x55998 for the frame (640x360 for the downsampled passes), and
 * (0, 512, 448, 0) at 0x554A8 for the menus' layout. Both are widened to the
 * window's aspect around their middle, so the 2D keeps its shape in the 16:9
 * middle of the screen: menus, movies, loading screens. On a window narrower
 * than 16:9 the same narrows them: the middle of the layout fills the window
 * at its own shape (hud_layout.h). The screen effects
 * (func_0029BDC8, func_0029C030: the blend of the previous frame, film grain)
 * and the HUD's damage overlays (one per side, drawn from 0x4CD830, 0x4CD908,
 * 0x4CD9D0 and 0x4CE4B8) keep theirs and cover
 * the whole screen; they are found up the guest stack.
 *
 * The HUD is then moved to the screen's sides:
 *  - its quads go through func_00227A1C(batch r3, uv r4, rect r5 = x0 y0 x1
 *    y1, y scale and offset r6, colour r7), from func_004C6B78..0x4CEC70 and
 *    the overlays at func_00223DB0..func_002263F8. Each quad goes with the
 *    third of the screen its middle is in, sized by the HUD size around that
 *    side, and the sides spread from the 16:9 middle (HUD position 0) to the
 *    screen's edges (1). A quad across the screen takes the whole width.
 *  - its text goes through func_00044108 (f1/f2 the position, f4 the
 *    glyphs' size), called from the HUD up to six calls above: the same, by
 *    its position. Laid-out text (func_0023A588: the tutorial's lines, with
 *    buttons in them) moves and scales as one block around its origin.
 *  - the tutorial's banner (func_004C7A28: bar, controller, line) is one
 *    element at the top middle, scaled around it and kept in the 16:9 middle.
 */
#include "native_ppu.h"
#include "../view.h"
#include "hud_sites.h"
#include "hud_layout.h"
#include "crosshair.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

extern "C" int rsx_vulkan_output_size(int* w, int* h);

namespace {

constexpr float kW = 1280.0f, kH = 720.0f, k169 = 16.0f / 9.0f;
constexpr uint32_t kOrthoRet = KZ_GA(0x0005599C), kMenuOrthoRet = KZ_GA(0x000554AC);

/* The 2D layer's width over 16:9's (hud_layout.h): 1 at 16:9, more for a
 * wider window, less for a narrower one. */
float widen() { return hud_widen(kz_view.aspect); }

bool log_on()
{
    static int on = -1;
    if (on < 0) on = getenv("KZ_CAM_LOG") ? 1 : 0;
    return on;
}

/* The guest's return addresses up the stack: the back chain at 0(r1), each
 * caller's saved LR at 16 bytes into its frame. */
int guest_chain(const ppu_context* ctx, uint32_t* out, int max)
{
    uint32_t sp = (uint32_t)ctx->gpr[1];
    int n = 0;
    while (n < max) {
        const uint32_t up = (uint32_t)vm_read64(sp);
        if (up <= sp || up - sp > 0x10000) break;
        sp = up;
        out[n++] = (uint32_t)vm_read64(sp + 16);
    }
    return n;
}

/* The screen effects (func_0029BDC8 and func_0029C030: the blend of the
 * previous frame, film grain) cover the whole screen: they keep the ortho
 * the frame stretches. Drawing is deferred, so the drawing object's caller
 * is found up the stack, a few frames above the ortho. */
bool full_screen_effect(const ppu_context* ctx)
{
    uint32_t ch[8];
    const int n = guest_chain(ctx, ch, 8);
    for (int i = 0; i < n; i++)
        if ((ch[i] >= KZ_GA(0x0029BDC8) && ch[i] < KZ_GA(0x0029C444)) || ch[i] == KZ_GA(0x004CD834) || ch[i] == KZ_GA(0x004CD90C) || ch[i] == KZ_GA(0x004CD9D4) ||
            ch[i] == KZ_GA(0x004CE4BC)) return true;
    return false;
}

bool in_hud(uint32_t a) { return a >= KZ_GA(0x004C6B78) && a < KZ_GA(0x004CEC70); }
/* The tutorial's banner (func_004C7A28: its bar, the controller and the line
 * of text) is one element at the top middle: it scales with the HUD around
 * that point and stays in the 16:9 middle, so its pieces stay together. */
bool in_tutorial(uint32_t a) { return a >= KZ_GA(0x004C7A28) && a < KZ_GA(0x004C8980); }
/* The laid-out text being drawn (func_0023A588) from the HUD: its origin as
 * the game placed it and where it goes; 0 scale outside one. */
double s_rich_x0, s_rich_y0, s_rich_nx, s_rich_ny, s_rich_k;
/* the 1280x720 layout to one view's band: 1, 0.5 in split screen (hud_layout.h) */
double band_k() { return hud_band_k(kz_view.players); }
double tut_x(double x) { const double k = kz_view.hud_scale > 0.0f ? kz_view.hud_scale : 1.0; return kW * 0.5 + (x - kW * 0.5) * k * band_k(); }

/* A HUD point (x, y), in the 1280x720 layout, to its place: it goes with the
 * third of the screen it is in (in y too), sized by the HUD size around that
 * side, and the sides spread from the 16:9 middle (position 0) to the screen's
 * edges (position 1), in the widened 2D layer; on a window narrower than 16:9
 * they are at its edges (hud_spread). ax/ay: the sides used. */
double hud_side_x(double x, double ax, double e)
{
    const double size = kz_view.hud_scale > 0.0f ? kz_view.hud_scale : 1.0;
    return hud_place_x(x, ax, kW, hud_spread(e, kz_view.hud_pos), size * band_k());
}
/* y of a HUD point in the 1280x720 layout that goes with side ay, in its band */
double hud_side_y(double y, double ay)
{
    const double size = kz_view.hud_scale > 0.0f ? kz_view.hud_scale : 1.0;
    return hud_place_y(y, ay, band_k(), size);
}
/* the height of one view's band: 720, 360 in split screen */
double band_h() { return hud_band_h(kz_view.players); }

/* The sides a HUD quad or string goes with (hud_layout.h hud_anchor). Not
 * kept per drawing site: one site draws the weapon's icon both in the ammo
 * panel and over a pickup prompt in the middle, and the bullets both in the
 * panel and as a pickup's "+8". */
void anchor_of(double cx, double cy, double* ax, double* ay) { hud_anchor(cx, cy, kW, kH, ax, ay); }

/* A HUD string's glyphs (func_00044108: f4 their width's scale, f5 the
 * font's height) to the HUD size and one view's band (hud_layout.h hud_glyph). */
void glyph(ppu_context* ctx, double size, double k)
{
    double w = ctx->fpr[4], h = ctx->fpr[5];
    hud_glyph(&w, &h, size, k);
    ctx->fpr[4] = S(w);
    if (k != 1.0) ctx->fpr[5] = S(h);
}

/* The HUD's own drawing, up the guest stack: its module and the overlays and
 * prompts next to it. */
bool hud_chain(const ppu_context* ctx)
{
    uint32_t ch[8];
    const int n = guest_chain(ctx, ch, 8);
    for (int i = 0; i < n; i++)
        if (in_hud(ch[i]) || (ch[i] >= KZ_GA(0x00223DB0) && ch[i] < KZ_GA(0x00227A1C))) return true;
    return false;
}

/* func_003CEBB8(r3 = the display): its viewport, the rectangle (x0, y0, x1,
 * y1, 32-bit ints) at r3 + 0x20; reached through the display's vtable
 * (+0x168). The 2D helpers func_002CA46C..func_002CACA8 map their layout to
 * that viewport. ENHANCEMENT: in split screen, asked by those helpers for the
 * HUD, it answers a copy twice as tall, so the HUD lays itself out in a
 * 1280x720 layout exactly as with one player; func_00227A1C and func_00044108
 * then scale that layout into the view's band (hud_layout.h hud_band_k). The
 * game itself lays the split HUD out in the view's 1280x360: heights halve,
 * the panels' end pieces and the elements' places keep their widths, so its
 * panels come out squashed and the ammo count stands higher on its bar than
 * with one player (RPCS3 and the port, 2026-11-25). */
void hud_viewport(ppu_context* ctx)
{
    const uint32_t vp = (uint32_t)ctx->gpr[3] + 0x20;
    ctx->gpr[3] = vp;
    if (kz_view.players != 2) return;
    /* asked by the 2D helpers (func_002CA9A8 five times: the text's size),
     * each reading the rectangle at once. Not doubled for func_004CB798
     * (0x004CC740): it sets the display's viewport from what it reads
     * (KZ_HUD_SITES runs, 2026-11-25) */
    const uint32_t lr = (uint32_t)ctx->lr;
    /* ENHANCEMENT: func_004CB798 draws the second player's HUD 20 pixels
     * up: for view 1 it reads this rectangle (0x004CC740) and sets it back
     * with y0 and y1 less 20 (0x004CC71C..0x004CC77C; the view's 2D viewport
     * is 0, 340, 1280x360 against 0, 360, VPLOG 2026-11-25; RPCS3 the same).
     * With one player's HUD in each band that put player 2's timer and
     * scores on the split line; answered 20 lower, its HUD keeps player 1's
     * margins (hud_layout.h hud_view_lift). */
    const int lift = lr == KZ_GA(0x004CC740) ? hud_view_lift(kz_view.players) : 0;
    if (!lift && (!(lr > KZ_GA(0x002CA46C) && lr < KZ_GA(0x002CADA8)) || !hud_chain(ctx))) return;
    /* the copy goes below the caller's frame (red zone), read before its next call */
    const uint32_t tmp = ((uint32_t)ctx->gpr[1] - 0x200) & ~15u;
    const int32_t y0 = (int32_t)vm_read32(vp + 4), y1 = (int32_t)vm_read32(vp + 12);
    vm_write32(tmp, vm_read32(vp));
    vm_write32(tmp + 4, (uint32_t)(y0 + lift));
    vm_write32(tmp + 8, vm_read32(vp + 8));
    vm_write32(tmp + 12, (uint32_t)(lift ? y1 + lift : y0 + 2 * (y1 - y0)));
    ctx->gpr[3] = tmp;
}

}  // namespace

extern "C" void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
/* stubs.cpp, at start */
void kz_hud_register(void) { ppu_register_function(KZ_GA(0x003CEBB8), hud_viewport); }

/* func_0005C498(out r3, left f1, right f2, bottom f3, top f4, near f5, far f6):
 * an orthographic projection. */
void func_0005C498(ppu_context* ctx)
{
    if (log_on()) {
        static uint32_t seen[64]; static int n;
        const uint32_t key = (uint32_t)ctx->lr ^ (uint32_t)ctx->fpr[2] << 12 ^ (uint32_t)ctx->fpr[3];
        int i = 0;
        while (i < n && seen[i] != key) i++;
        if (i == n && n < 64) {
            seen[n++] = key;
            fprintf(stderr, "[kz-ortho] from 0x%08X: l %g r %g b %g t %g n %g f %g\n", (uint32_t)ctx->lr,
                    ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], ctx->fpr[4], ctx->fpr[5], ctx->fpr[6]);
        }
    }
    if (log_on() && (uint32_t)ctx->lr == kOrthoRet && ctx->fpr[2] == kW) {
        uint32_t ch[12];
        const int n = guest_chain(ctx, ch, 12);
        static uint32_t seen[256]; static int ns;
        uint32_t key = 0;
        for (int i = 0; i < n; i++) key = key * 31 + ch[i];
        int i = 0;
        while (i < ns && seen[i] != key) i++;
        if (i == ns && ns < 256) {
            seen[ns++] = key;
            fprintf(stderr, "[kz-ortho-chain]");
            for (int k = 0; k < n; k++) fprintf(stderr, " %08X", ch[k]);
            fprintf(stderr, "\n");
        }
    }
    const float e = widen();
    /* the frame's 2D layer: 1280x720, or one view's 1280x360 in split screen */
    if (e != 1.0f && (uint32_t)ctx->lr == kOrthoRet && ctx->fpr[1] == 0.0 && ctx->fpr[2] == kW &&
        (ctx->fpr[3] == kH || ctx->fpr[3] == band_h()) && ctx->fpr[4] == 0.0 && !full_screen_effect(ctx)) {
        ctx->fpr[1] = S(kW * 0.5 - kW * 0.5 * e);
        ctx->fpr[2] = S(kW * 0.5 + kW * 0.5 * e);
    }
    if ((uint32_t)ctx->lr == kMenuOrthoRet) kz_view_menu++;
    /* KZ_HUD_SITES=1: who builds the menus' layout, each chain once */
    { static int on = -1; if (on < 0) on = getenv("KZ_HUD_SITES") ? 1 : 0;
      if (on && (uint32_t)ctx->lr == kMenuOrthoRet) {
          uint32_t ch[10]; const int m = guest_chain(ctx, ch, 10);
          static uint32_t seen[256]; static int ns; uint32_t key = 0;
          for (int i = 0; i < m; i++) key = key * 31 + ch[i];
          int i = 0; while (i < ns && seen[i] != key) i++;
          if (i == ns && ns < 256) { seen[ns++] = key; fprintf(stderr, "[kz-menu-ortho]");
              for (int k = 0; k < m; k++) fprintf(stderr, " %08X", ch[k]);
              fprintf(stderr, "\n"); } } }
    /* the menus' layout, 512x448 (built at 0x554A8) */
    if (e != 1.0f && (uint32_t)ctx->lr == kMenuOrthoRet && ctx->fpr[1] == 0.0 && ctx->fpr[2] == 512.0 &&
        ctx->fpr[4] == 0.0) {
        ctx->fpr[1] = S(256.0 - 256.0 * e);
        ctx->fpr[2] = S(256.0 + 256.0 * e);
    }
    func_0005C498_lifted(ctx);
}

void func_00227A1C(ppu_context* ctx)
{
    const uint32_t ret = (uint32_t)ctx->lr, rect = (uint32_t)ctx->gpr[5];
    const bool hud = in_hud(ret) || (ret >= KZ_GA(0x00223DB0) && ret < KZ_GA(0x00227A1C));
    if (log_on()) {
        /* each drawing site with each rectangle (to whole pixels), once */
        static uint32_t seen[1024]; static int n;
        const uint32_t key = ret * 31u + (uint32_t)(int)ldf(rect) * 7u + (uint32_t)(int)ldf(rect + 4) * 131u +
                             (uint32_t)(int)ldf(rect + 8) * 1031u + (uint32_t)(int)ldf(rect + 12) * 8191u;
        int i = 0;
        while (i < n && seen[i] != key) i++;
        if (i == n && n < 1024) {
            seen[n++] = key;
            fprintf(stderr, "[kz-quad] from 0x%08X: %g %g %g %g y*%g%+g\n", ret, ldf(rect), ldf(rect + 4),
                    ldf(rect + 8), ldf(rect + 12), ldf((uint32_t)ctx->gpr[6]), ldf((uint32_t)ctx->gpr[6] + 4));
        }
    }
    /* KZ_HUD_SITES=1 (diagnostic): each drawing site once, with the first
     * rectangle it draws in each third of the screen */
    { static int on = -1; if (on < 0) on = getenv("KZ_HUD_SITES") ? 1 : 0;
      if (on) { static uint32_t seen[512]; static int n; int i = 0;
          const double cx = (ldf(rect) + ldf(rect + 8)) * 0.5;
          const uint32_t key = ret * 4u + (cx < kW / 3 ? 0u : cx > kW * 2 / 3 ? 2u : 1u);   /* once per site and third */
          while (i < n && seen[i] != key) i++;
          if (i == n && n < 512) { seen[n++] = key;
              fprintf(stderr, "[kz-hud-site] 0x%08X hud %d: %g %g %g %g (middle x %g: %s third)\n", ret, hud ? 1 : 0,
                      ldf(rect), ldf(rect + 4), ldf(rect + 8), ldf(rect + 12), cx,
                      cx < kW / 3 ? "left" : cx > kW * 2 / 3 ? "right" : "middle"); } } }
    /* KZ_HUD_SKIP=0xA,0xB (diagnostic): those drawing sites draw nothing;
     * KZ_HUD_TRACE=0xA,0xB: each rectangle they draw with a new size,
     * texture coordinates or place (to whole units), once, for 1000 of them */
    { static uint32_t skip[8], trace[8]; static int ns = -1, nt;
      if (ns < 0) { ns = 0;
          auto parse = [](const char* s, uint32_t* o, int* n) { while (s && *s && *n < 8) { char* e2; o[(*n)++] = (uint32_t)strtoul(s, &e2, 0); s = *e2 ? e2 + 1 : e2; } };
          parse(getenv("KZ_HUD_SKIP"), skip, &ns); parse(getenv("KZ_HUD_TRACE"), trace, &nt); }
      for (int i = 0; i < nt; i++) if (trace[i] == ret) {
          static uint64_t seen[1000]; static int n;
          const uint32_t uv = (uint32_t)ctx->gpr[4];
          uint64_t key = ret;
          /* its size and texture exactly, its place to whole units */
          const double w = ldf(rect + 8) - ldf(rect), h = ldf(rect + 12) - ldf(rect + 4);
          key = key * 1000003u + (uint64_t)llround(w * 1000) * 31u + (uint64_t)llround(h * 1000);
          key = key * 1000003u + (uint64_t)llround(ldf(rect) + w * 0.5) * 7919u + (uint64_t)llround(ldf(rect + 4) + h * 0.5);
          for (int k = 0; k < 16; k += 4) key = key * 1000003u + vm_read32(uv + k);
          int j = 0; while (j < n && seen[j] != key) j++;
          if (j == n && n < 1000) { seen[n++] = key;
              fprintf(stderr, "[kz-hud-trace] 0x%08X: %g %g %g %g uv %g %g %g %g y*%g%+g\n", ret, ldf(rect), ldf(rect + 4),
                      ldf(rect + 8), ldf(rect + 12), ldf(uv), ldf(uv + 4), ldf(uv + 8), ldf(uv + 12),
                      ldf((uint32_t)ctx->gpr[6]), ldf((uint32_t)ctx->gpr[6] + 4)); } }
      for (int i = 0; i < ns; i++) if (skip[i] == ret) return; }
    /* ENHANCEMENT: the weapon crosshair (crosshair.h) keeps the place the
     * game gives it, outside the HUD Size rule; its pieces take their own
     * size, on whole pixels of the output */
    if (crosshair_site(ret)) {
        const double cs = crosshair_clamp(kz_view.crosshair_size > 0.0f ? kz_view.crosshair_size : 1.0), k = band_k();
        if (cs == 1.0 && k == 1.0) {
            func_00227A1C_lifted(ctx);
            return;
        }
        const uint32_t ys = (uint32_t)ctx->gpr[6];
        const double sy = ldf(ys), ty = ldf(ys + 4);
        if (sy == 0.0) {
            func_00227A1C_lifted(ctx);
            return;
        }
        double r[4] = { ldf(rect), ldf(rect + 4) * sy + ty, ldf(rect + 8), ldf(rect + 12) * sy + ty };
        if (k != 1.0)   /* split screen: from the 1280x720 layout (hud_viewport) into the band, around the middle */
            for (int i = 0; i < 4; i++) r[i] = i & 1 ? hud_place_y(r[i], kH * 0.5, k, 1.0) : kW * 0.5 + (r[i] - kW * 0.5) * k;
        if (cs != 1.0) {
            int ow = 0, oh = 0;
            const double lw = kW * widen();
            if (!rsx_vulkan_output_size(&ow, &oh)) { ow = (int)lround(lw); oh = (int)kH; }   /* no window: the frame's own */
            /* in split screen one view's band: its share of the output, its height */
            crosshair_rect(r, cs, ow, oh * band_h() / kH, lw, band_h());
        }
        const uint32_t tmp = ((uint32_t)ctx->gpr[1] - 0x400) & ~15u;
        stf(tmp, r[0]); stf(tmp + 4, (r[1] - ty) / sy); stf(tmp + 8, r[2]); stf(tmp + 12, (r[3] - ty) / sy);
        ctx->gpr[5] = tmp;
        func_00227A1C_lifted(ctx);
        return;
    }
    const float e = widen(), size = kz_view.hud_scale > 0.0f ? kz_view.hud_scale : 1.0f;
    const double k = band_k();
    if (!hud || (e == 1.0f && size == 1.0f && k == 1.0)) {
        func_00227A1C_lifted(ctx);
        return;
    }
    /* the rectangle is read through r5: a copy on the guest stack, below the
     * caller's frame (red zone), is moved and passed instead */
    double x0 = ldf(rect), y0 = ldf(rect + 4), x1 = ldf(rect + 8), y1 = ldf(rect + 12);
    if (in_tutorial(ret)) {
        const uint32_t ys = (uint32_t)ctx->gpr[6];
        const double sy = ldf(ys), ty = ldf(ys + 4);
        x0 = tut_x(x0); x1 = tut_x(x1);
        /* around the screen's top */
        y0 = hud_place_y_quad(y0, 0.0, sy, ty, k, size); y1 = hud_place_y_quad(y1, 0.0, sy, ty, k, size);
    } else if (std::min(x0, x1) <= 0.0 && std::max(x0, x1) >= kW) {   /* the whole width: an overlay */
        x0 = kW * 0.5 + (x0 - kW * 0.5) * e;
        x1 = kW * 0.5 + (x1 - kW * 0.5) * e;
        if (k != 1.0) {   /* split screen: its height into the band */
            const uint32_t ys = (uint32_t)ctx->gpr[6];
            const double sy = ldf(ys), ty = ldf(ys + 4);
            y0 = hud_place_y_quad(y0, 0.0, sy, ty, k, 1.0); y1 = hud_place_y_quad(y1, 0.0, sy, ty, k, 1.0);
        }
    } else {
        const uint32_t ys = (uint32_t)ctx->gpr[6];
        const double sy = ldf(ys), ty = ldf(ys + 4);
        const double cx = (x0 + x1) * 0.5, cy = ((y0 + y1) * 0.5) * sy + ty;
        double site_ax, ay_out;
        anchor_of(cx, cy, &site_ax, &ay_out);
        if (hud_site_side(ret) == HUD_SIDE_RIGHT) {   /* hud_sites.h: the grenade row goes with the ammo panel */
            site_ax = kW;
            ay_out = cy < kH * 0.5 ? 0.0 : kH;
        }
        const double lo = std::min(x0, x1), hi = std::max(x0, x1);
        if (lo < kW / 3 && hi > kW * 2 / 3) {
            /* across the screen (a banner): each end goes with its own side */
            const bool fwd = x0 <= x1;
            x0 = hud_side_x(x0, fwd ? 0.0 : kW, e); x1 = hud_side_x(x1, fwd ? kW : 0.0, e);
        } else {
            x0 = hud_side_x(x0, site_ax, e); x1 = hud_side_x(x1, site_ax, e);
        }
        y0 = hud_place_y_quad(y0, ay_out, sy, ty, k, size); y1 = hud_place_y_quad(y1, ay_out, sy, ty, k, size);
    }
    const uint32_t tmp = ((uint32_t)ctx->gpr[1] - 0x400) & ~15u;
    stf(tmp, x0); stf(tmp + 4, y0); stf(tmp + 8, x1); stf(tmp + 12, y1);
    ctx->gpr[5] = tmp;
    func_00227A1C_lifted(ctx);
}

/* func_00044108: draws a 2D object (r3) at f1/f2. */
void func_00044108(ppu_context* ctx)
{
    /* the HUD's text: the string's position goes with its side, its size
     * (f4) with the HUD's */
    {
        uint32_t ch[6];
        const int m = guest_chain(ctx, ch, 6);
        /* the HUD's own module and the overlays and prompts next to its
         * drawing (func_00223DB0..: "Hold X to pick up", whose weapon icon
         * goes through func_00227A1C from there) */
        auto ours = [](uint32_t a) { return in_hud(a) || (a >= KZ_GA(0x00223DB0) && a < KZ_GA(0x00227A1C)); };
        bool hud = ours((uint32_t)ctx->lr), rich = false;
        for (int i = 0; i < m && !hud; i++) hud = ours(ch[i]);
        /* laid-out text (func_0023A588: the tutorial's lines, with buttons in
         * them) is moved and sized as one block, there */
        for (int i = 0; i < m; i++) rich |= ch[i] == KZ_GA(0x0023A734) || ch[i] == KZ_GA(0x0023A7B0);
        const float e = widen(), size = kz_view.hud_scale > 0.0f ? kz_view.hud_scale : 1.0f;
        const double k = band_k();
        if (rich && s_rich_k > 0.0) {
            /* a run of the laid-out text: around the block's origin */
            ctx->fpr[1] = S(s_rich_nx + (ctx->fpr[1] - s_rich_x0) * s_rich_k);
            ctx->fpr[2] = S(s_rich_ny + (ctx->fpr[2] - s_rich_y0) * s_rich_k);
            /* the glyphs' size is f4 (the font's height in f5 over its own; f3
             * aligns the string, f5 also spaces the glyphs) */
            glyph(ctx, size, k);
        }
        { static int on = -1; if (on < 0) on = getenv("KZ_HUD_SITES") ? 1 : 0;
          if (on && (hud || ctx->fpr[2] > 540.0)) { static uint32_t seen[512]; static int n; int i = 0;
              const double x = ctx->fpr[1];
              const uint32_t key = ((uint32_t)ctx->lr ^ (m > 1 ? ch[1] << 1 : 0)) * 4u + (x < kW / 3 ? 0u : x > kW * 2 / 3 ? 2u : 1u);
              while (i < n && seen[i] != key) i++;
              if (i == n && n < 512) { seen[n++] = key;
                  fprintf(stderr, "[kz-hud-text] 0x%08X (%08X %08X) hud %d: x %g y %g size %g (%s third)\n", (uint32_t)ctx->lr,
                          m > 0 ? ch[0] : 0, m > 1 ? ch[1] : 0, hud ? 1 : 0, x, ctx->fpr[2], ctx->fpr[4],
                          x < kW / 3 ? "left" : x > kW * 2 / 3 ? "right" : "middle"); } } }
        if (hud && !rich && (e != 1.0f || size != 1.0f || k != 1.0)) {
            const double x = ctx->fpr[1], y = ctx->fpr[2];
            double ax, ay;
            anchor_of(x, y, &ax, &ay);
            ctx->fpr[1] = S(hud_side_x(x, ax, e));
            ctx->fpr[2] = S(hud_side_y(y, ay));
            /* the glyphs' size is f4 (the font's height in f5 over its own; f3
             * aligns the string, f5 also spaces the glyphs) */
            glyph(ctx, size, k);
        }
    }
    if (log_on()) {
        static uint32_t seen[128]; static int n;
        uint32_t ch[6];
        const int m = guest_chain(ctx, ch, 6);
        const uint32_t ret = (uint32_t)ctx->lr, key = ret ^ (m > 1 ? ch[1] << 1 : 0);
        int i = 0;
        while (i < n && seen[i] != key) i++;
        if (i == n && n < 128) {
            seen[n++] = key;
            fprintf(stderr, "[kz-obj] from 0x%08X (%08X %08X): f1 %g f2 %g f3 %g f4 %g f5 %g r3 %08X r4 %08X r5 %08X r6 %08X r9 %08X\n", ret,
                    m > 0 ? ch[0] : 0, m > 1 ? ch[1] : 0,
                    ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], ctx->fpr[4], ctx->fpr[5], (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4],
                    (uint32_t)ctx->gpr[5], (uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[9]);
        }
    }
    func_00044108_lifted(ctx);
}

/* func_0023A588: draws laid-out text (r3) at f1/f2 with scale f3/f4 (the
 * tutorial's lines, buttons included). From the HUD it moves with its side
 * and scales as one block, so the buttons stay in the words. */
void func_0023A588(ppu_context* ctx)
{
    uint32_t ch[3];
    const int m = guest_chain(ctx, ch, 3);
    bool hud = in_hud((uint32_t)ctx->lr);
    for (int i = 0; i < m && !hud; i++) hud = in_hud(ch[i]);
    const float e = widen(), size = kz_view.hud_scale > 0.0f ? kz_view.hud_scale : 1.0f;
    const double k = band_k();
    if (log_on() && hud) { static int n; if (n++ < 30) fprintf(stderr, "[kz-rich] lr %08X hud %d f1 %g f2 %g f3 %g f4 %g\n",
        (uint32_t)ctx->lr, hud, ctx->fpr[1], ctx->fpr[2], ctx->fpr[3], ctx->fpr[4]); }
    if (hud && (e != 1.0f || size != 1.0f || k != 1.0)) {
        /* the block's origin goes with its side (the tutorial's with the
         * banner, at the top middle); its runs follow it, scaled
         * (func_00044108). The game lays the runs out itself from f1/f2. */
        const double x = ctx->fpr[1], y = ctx->fpr[2];
        double ax, ay;
        anchor_of(x, y, &ax, &ay);
        const bool tut = in_tutorial((uint32_t)ctx->lr);
        s_rich_x0 = x; s_rich_y0 = y; s_rich_k = size * k;
        s_rich_nx = tut ? tut_x(x) : hud_side_x(x, ax, e);
        s_rich_ny = tut ? hud_place_y(y, 0.0, k, size) : hud_side_y(y, ay);
    }
    func_0023A588_lifted(ctx);
    s_rich_k = 0.0;
}
