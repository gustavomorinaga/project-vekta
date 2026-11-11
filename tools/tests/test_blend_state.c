/* The blend attachments of a pipeline and the independentBlend feature (patch 2061). Before it the feature was enabled
 * only under a temporal upscaler, but the pipeline list on disk kept the velocity pipelines a run with an upscaler had
 * made, and a run without one built them ahead: target A with the title's blend and mask, the velocity target with
 * none, on a device without independentBlend, which the validation layer reports as
 * VUID-VkPipelineColorBlendStateCreateInfo-pAttachments-00605. */
#include <stdio.h>
#include <string.h>
#include "rsx_blend_state.h"

static int fail(const char* why) { fprintf(stderr, "FAIL: %s\n", why); return 1; }

/* VkPipelineColorBlendAttachmentState's layout (eight 32-bit fields), without the Vulkan headers */
typedef struct { unsigned enable, src_c, dst_c, op_c, src_a, dst_a, op_a, mask; } att;

int main(void)
{
    /* the velocity variant needs all three: an upscaler, per-object motion and the feature */
    if (!rsx_vk_vel_on(1, 1, 1)) return fail("no velocity variant with an upscaler on a device with independentBlend");
    if (rsx_vk_vel_on(0, 1, 1)) return fail("a velocity variant without an upscaler");
    if (rsx_vk_vel_on(2, 0, 1)) return fail("a velocity variant with per-object motion off");
    if (rsx_vk_vel_on(2, 1, 0)) return fail("a velocity variant on a device without independentBlend");

    /* the list on disk: plain records always, velocity ones only in a run that makes them (the bug: all of them) */
    if (!rsx_vk_plist_rec_wanted(0u, 0) || !rsx_vk_plist_rec_wanted(0u, 1)) return fail("a plain record skipped");
    if (rsx_vk_plist_rec_wanted(1u, rsx_vk_vel_on(0, 1, 1)))
        return fail("a velocity record from a run with an upscaler built in a run without one");
    if (!rsx_vk_plist_rec_wanted(1u, rsx_vk_vel_on(2, 1, 1))) return fail("a velocity record skipped under an upscaler");

    /* the attachments themselves: what vk_eng_build makes for two title targets and the velocity target */
    att a[3];
    memset(a, 0, sizeof a);
    a[0] = (att){ 1, 6, 7, 0x8006, 1, 0, 0x8006, 0xF };   /* blended, as the title's smoke and glow */
    a[1] = a[0];                                          /* every title target takes target A's state */
    a[2] = (att){ .mask = 0 };                            /* the velocity target: no blend, masked for blended draws */
    if (!rsx_vk_blend_attachments_ok(a, sizeof a[0], 2, 0)) return fail("identical title targets refused");
    if (rsx_vk_blend_attachments_ok(a, sizeof a[0], 3, 0)) return fail("the velocity target allowed without the feature");
    if (!rsx_vk_blend_attachments_ok(a, sizeof a[0], 3, 1)) return fail("the velocity target refused with the feature");
    if (!rsx_vk_blend_attachments_ok(a, sizeof a[0], 1, 0)) return fail("one attachment refused");
    a[1].mask = 0x7;                                      /* a mask that differs is the same rule */
    if (rsx_vk_blend_attachments_ok(a, sizeof a[0], 2, 0)) return fail("different masks allowed without the feature");

    puts("test_blend_state: ok");
    return 0;
}
