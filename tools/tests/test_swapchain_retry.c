/* A swapchain that could not be recreated (patch 2057): the present tries again on the next presents, then once in
 * RSX_SC_RETRY_EVERY, never gives up, and logs the first failure and one in 100. Before the patch the first failure
 * closed the window for good ("window disabled, continuing headless: the swapchain could not be recreated"). */
#include <stdio.h>
#include "rsx_swapchain_retry.h"

static int fail(const char* why) { fprintf(stderr, "FAIL: %s\n", why); return 1; }

/* Simulates n presents with the recreate failing on every try; returns the number of tries. */
static unsigned tries_in(unsigned n)
{
    unsigned fails = 1, skipped = 0, tries = 0;   /* the first failure has happened */
    for (unsigned p = 0; p < n; p++) {
        if (!rsx_sc_retry_due(fails, skipped)) { skipped++; continue; }
        tries++; fails++; skipped = 0;
    }
    return tries;
}

int main(void)
{
    if (!rsx_sc_retry_due(1, 0) || !rsx_sc_retry_due(2, 0)) return fail("the first tries are not on consecutive presents");
    if (rsx_sc_retry_due(RSX_SC_RETRY_BURST, 0)) return fail("still a try on every present after the burst");
    if (!rsx_sc_retry_due(RSX_SC_RETRY_BURST + 50u, RSX_SC_RETRY_EVERY - 1u)) return fail("gave up after the burst");
    const unsigned t = tries_in(3000);
    printf("3000 presents with a recreate that always fails: %u tries\n", t);
    if (t < 3000u / RSX_SC_RETRY_EVERY) return fail("too few tries: the window would stay dark");
    if (t > RSX_SC_RETRY_BURST + 3000u / RSX_SC_RETRY_EVERY + 1u) return fail("too many tries (each waits for the device)");
    if (!rsx_sc_retry_logs(1) || rsx_sc_retry_logs(2) || !rsx_sc_retry_logs(100)) return fail("log rule");
    puts("test_swapchain_retry: ok");
    return 0;
}
