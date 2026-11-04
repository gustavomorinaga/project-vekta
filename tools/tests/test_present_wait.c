/* Patch 2042 (port issue #18): a FIFO swapchain the player did not ask for gets
 * a bounded acquire, so the present never holds the frame clock (the guest's
 * vblanks and FIFO drain) until the display is ready. Checks the policy in
 * rsx_vk_present_wait.h, from the patched tree. No GPU. */
#include "rsx_vk_present_wait.h"
#include <stdint.h>
#include <stdio.h>

static int failures;
static void check(int ok, const char* what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; } }

int main(void)
{
    /* got, want */
    check(rsx_vk_present_bounded(RSX_VK_PM_FIFO, RSX_VK_PM_MAILBOX), "mailbox asked, FIFO given (AMD on Windows): bounded");
    check(rsx_vk_present_bounded(RSX_VK_PM_FIFO_RELAXED, RSX_VK_PM_FIFO_RELAXED), "adaptive vsync: bounded");
    check(rsx_vk_present_bounded(RSX_VK_PM_FIFO, RSX_VK_PM_IMMEDIATE), "vsync off, FIFO given: bounded");
    check(!rsx_vk_present_bounded(RSX_VK_PM_FIFO, RSX_VK_PM_FIFO), "vsync = fifo (VRR) keeps the display's wait");
    check(!rsx_vk_present_bounded(RSX_VK_PM_MAILBOX, RSX_VK_PM_MAILBOX), "mailbox never waits: nothing to bound");
    check(!rsx_vk_present_bounded(RSX_VK_PM_IMMEDIATE, RSX_VK_PM_IMMEDIATE), "immediate never waits: nothing to bound");

    check(rsx_vk_acquire_timeout_ns(0, NULL) == UINT64_MAX, "not bounded: waits");
    check(rsx_vk_acquire_timeout_ns(0, "5") == UINT64_MAX, "not bounded: the variable does not matter");
    check(rsx_vk_acquire_timeout_ns(1, NULL) == 0, "bounded, default: no wait");
    check(rsx_vk_acquire_timeout_ns(1, "") == 0, "bounded, empty variable: no wait");
    check(rsx_vk_acquire_timeout_ns(1, "4") == 4000000u, "PS3RECOMP_VK_PRESENT_WAIT_MS=4: 4 ms");
    check(rsx_vk_acquire_timeout_ns(1, "1000") == 100000000u, "capped at 100 ms");
    check(rsx_vk_acquire_timeout_ns(1, "-1") == UINT64_MAX, "PS3RECOMP_VK_PRESENT_WAIT_MS=-1: the old wait");
    printf("test_present_wait: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
