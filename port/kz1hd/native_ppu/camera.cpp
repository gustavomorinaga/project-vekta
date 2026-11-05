/*
 * The camera's projection, for widescreen and the FOV setting. See native_ppu.h.
 *
 * func_001A9FC8(out r3, camera r4) builds a camera's projection: near +0x68,
 * far +0x6C, vertical FOV in degrees +0x70, aspect +0x74, perspective when the
 * byte at +0x78 is set (+0x80 set: +0x70 is horizontal and is converted with
 * the aspect first). Every frame the game copies FOV and aspect into the
 * world camera (func_002B7CD0, which calls this from 0x2B81D8) and the
 * first-person model's camera (func_001FD9C0, built from 0x21A18C).
 *
 * The game's aspect is the viewport's, 1280/720, and its FOV is 100 degrees
 * divided by it (56.25 vertical at 16:9). Here a perspective camera with the
 * 16:9 aspect gets the window's aspect instead (the frame is rendered
 * anamorphic and stretched to the window), keeping its vertical FOV: the
 * picture widens (Hor+) and nothing is stretched. The world camera also takes
 * the FOV setting, while the first-person model is drawn, as a scale of the
 * game's own FOV, so zooming keeps its magnification; the model keeps the
 * game's FOV so the weapon is framed as the game made it.
 */
#include "native_ppu.h"
#include "../view.h"
#include <stdio.h>
#include <stdlib.h>

extern "C" {
int rsx_temporal_next_jitter(float* jitter_x, float* jitter_y);
void rsx_temporal_set_view(const float vp[16], float jitter_x, float jitter_y,
                           float near_z, float far_z, float fov_y);
}

namespace {

constexpr uint32_t kWorldRet = 0x002B81DC, kModelRet = 0x0021A190;
constexpr float kGameAspect = 16.0f / 9.0f, kGameFov = 56.25f;
constexpr double kDeg = 3.14159265358979323846 / 180.0;

unsigned s_world_n, s_model_n;   // world projections built, and the count when the model's last was

void log_camera(uint32_t cam, uint32_t ret)
{
    static int on = -1;
    if (on < 0) on = getenv("KZ_CAM_LOG") ? 1 : 0;
    if (!on) return;
    static uint32_t seen[64][2];
    static int n;
    for (int i = 0; i < n; i++) if (seen[i][0] == cam && seen[i][1] == ret) return;
    if (n == 64) return;
    seen[n][0] = cam; seen[n][1] = ret; n++;
    fprintf(stderr, "[kz-cam] 0x%08X from 0x%08X: near %g far %g fov %g aspect %g persp %u hfov %u\n", cam, ret,
            ldf(cam + 0x68), ldf(cam + 0x6C), ldf(cam + 0x70), ldf(cam + 0x74),
            vm_read8(cam + 0x78), vm_read8(cam + 0x80));
}

/* Temporal upscalers (rsx_temporal.h in ps3recomp): the world and the
 * first-person model are drawn with this frame's sub-pixel jitter, an
 * off-centre frustum. out: the projection built, camera position +0x04,
 * forward +0x10, up +0x1C, near +0x28, far +0x2C, frustum tangents left +0x30
 * right +0x34 top +0x38 bottom +0x3C. KZ_JITTER_TEST=x,y: a constant offset
 * instead, in pixels of the 1280x720 frame. */
float s_jx, s_jy;

/* The world's view-projection, as the render view the game makes of `out`
 * (func_001AA618, from 0x2B81F4) has it, but without the jitter: for a row
 * vector, the view's axes are forward x up, forward, up, and the projection
 * gives (x, depth, y, w). Handed on for column vectors giving (x, y, depth, w). */
void publish_view(uint32_t out)
{
    const double px = ldf(out + 0x04), py = ldf(out + 0x08), pz = ldf(out + 0x0C);
    const double a[3] = { ldf(out + 0x10), ldf(out + 0x14), ldf(out + 0x18) };
    const double b[3] = { ldf(out + 0x1C), ldf(out + 0x20), ldf(out + 0x24) };
    const double x[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    const double n = ldf(out + 0x28), f = ldf(out + 0x2C);
    const double l = ldf(out + 0x30), r = ldf(out + 0x34), t = ldf(out + 0x38), bt = ldf(out + 0x3C);
    if (r == l || t == bt || f == n) return;
    double v[4][4] = {
        { x[0], a[0], b[0], 0 }, { x[1], a[1], b[1], 0 }, { x[2], a[2], b[2], 0 },
        { -(px * x[0] + py * x[1] + pz * x[2]), -(px * a[0] + py * a[1] + pz * a[2]), -(px * b[0] + py * b[1] + pz * b[2]), 1 } };
    double pr[4][4] = {};
    pr[0][0] = 2.0 / (r - l); pr[1][0] = -(r + l) / (r - l);
    pr[1][1] = (f + n) / (f - n); pr[1][2] = -(t + bt) / (t - bt); pr[1][3] = 1.0;
    pr[2][2] = 2.0 / (t - bt); pr[3][1] = -2.0 * f * n / (f - n);
    static const int col[4] = { 0, 2, 1, 3 };
    float vp[16];
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 4; i++) {
            double s = 0.0;
            for (int j = 0; j < 4; j++) s += v[i][j] * pr[j][col[k]];
            vp[4 * k + i] = (float)s;
        }
    /* the frustum moves right and up: the picture moves left and down */
    rsx_temporal_set_view(vp, -s_jx, s_jy, (float)n, (float)f, (float)(2.0 * atan(0.5 * (t - bt))));
}

void jitter_frustum(uint32_t out, bool next)
{
    static int test = -1;
    static float tx, ty;
    if (test < 0) { const char* e = getenv("KZ_JITTER_TEST"); test = e && sscanf(e, "%f,%f", &tx, &ty) == 2; }
    if (next) {
        if (test) { s_jx = tx; s_jy = ty; }
        else rsx_temporal_next_jitter(&s_jx, &s_jy);
        publish_view(out);   /* before the jitter moves the frustum */
    }
    if (s_jx == 0.0f && s_jy == 0.0f) return;
    const float l = ldf(out + 0x30), r = ldf(out + 0x34), t = ldf(out + 0x38), b = ldf(out + 0x3C);
    const float dx = s_jx * (r - l) / 1280.0f, dy = s_jy * (t - b) / 720.0f;
    stf(out + 0x30, l + dx); stf(out + 0x34, r + dx);
    stf(out + 0x38, t + dy); stf(out + 0x3C, b + dy);
}

}  // namespace

void func_001A9FC8(ppu_context* ctx)
{
    const uint32_t cam = (uint32_t)ctx->gpr[4], ret = (uint32_t)ctx->lr, out = (uint32_t)ctx->gpr[3];
    log_camera(cam, ret);
    if (ret == kModelRet) s_model_n = s_world_n;
    if (!vm_read8(cam + 0x78)) {   /* orthographic */
        func_001A9FC8_lifted(ctx);
        return;
    }
    const float aspect = kz_view.aspect;
    if (aspect > 0.0f && fabs(ldf(cam + 0x74) - kGameAspect) < 0.01) stf(cam + 0x74, aspect);
    kz_view_3d++;   /* a 3D picture is being drawn (pcoptions.cpp: no crop) */
    if (ret != kWorldRet || !(kz_view.fov > 0.0f) || s_world_n++ - s_model_n > 2 || vm_read8(cam + 0x80)) {
        func_001A9FC8_lifted(ctx);
        if (ret == kWorldRet || ret == kModelRet) jitter_frustum(out, ret == kWorldRet);
        return;
    }
    /* tan(fov/2) scales with the setting, so a zoomed FOV keeps its magnification */
    const uint32_t fov_bits = vm_read32(cam + 0x70);
    const double fov = ldf(cam + 0x70);
    const double t = tan(fov * 0.5 * kDeg) * tan(kz_view.fov * 0.5 * kDeg) / tan(kGameFov * 0.5 * kDeg);
    stf(cam + 0x70, 2.0 * atan(t) / kDeg);
    func_001A9FC8_lifted(ctx);
    vm_write32(cam + 0x70, fov_bits);
    jitter_frustum(out, true);
}
