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

}  // namespace

void func_001A9FC8(ppu_context* ctx)
{
    const uint32_t cam = (uint32_t)ctx->gpr[4], ret = (uint32_t)ctx->lr;
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
        return;
    }
    /* tan(fov/2) scales with the setting, so a zoomed FOV keeps its magnification */
    const uint32_t fov_bits = vm_read32(cam + 0x70);
    const double fov = ldf(cam + 0x70);
    const double t = tan(fov * 0.5 * kDeg) * tan(kz_view.fov * 0.5 * kDeg) / tan(kGameFov * 0.5 * kDeg);
    stf(cam + 0x70, 2.0 * atan(t) / kDeg);
    func_001A9FC8_lifted(ctx);
    vm_write32(cam + 0x70, fov_bits);
}

