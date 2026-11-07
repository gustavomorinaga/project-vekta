/* The window present's transfer commands (patch 2053): when the frame has bars, the clear of the swapchain image and
 * the blit of the frame into it are separated by a barrier, and the blit comes last. Before the patch the present
 * cleared and blitted with nothing between them, and the clear overwrote parts of the frame on AMD (issue #17). */
#include <stdio.h>
#include "rsx_present_steps.h"

static int fail(const char* why) { fprintf(stderr, "FAIL: %s\n", why); return 1; }

int main(void)
{
    int s[3] = { 0, 0, 0 };
    int n = rsx_present_steps(1, s);
    if (n != 3) return fail("with bars: not clear, barrier, blit");
    if (s[0] != RSX_PRESENT_CLEAR) return fail("with bars: the clear is not first");
    if (s[1] != RSX_PRESENT_CLEAR_DONE) return fail("with bars: no barrier between the clear and the blit");
    if (s[2] != RSX_PRESENT_BLIT) return fail("with bars: the blit is not last");
    n = rsx_present_steps(0, s);
    if (n != 1 || s[0] != RSX_PRESENT_BLIT) return fail("without bars: only the blit");
    puts("test_present_steps: ok");
    return 0;
}
