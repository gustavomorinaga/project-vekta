/* PS3_GCM_MAX_QUEUED_FLIPS's wait (patch 2055): gcm_flip_wait() really waits, up to its bound, while the title is
 * ahead, and stops as soon as it is not, on Linux and on Windows (tools/wine-host-tests.sh runs this built for
 * Windows under Wine). Before the patch the Windows wait was 1000 rounds of Sleep(0), over within microseconds. */
#include <stdio.h>
#include "gcm_flip_wait.h"

static int always(void* arg) { (void)arg; return 1; }
static int countdown(void* arg) { int* n = (int*)arg; return (*n)-- > 0; }

int main(void)
{
    unsigned long long w = gcm_flip_wait(always, NULL, 100000);
    printf("ahead all along: waited %llu us (bound 100000)\n", w);
    if (w < 100000) { fprintf(stderr, "FAIL: gave up before its bound\n"); return 1; }
    if (w > 1000000) { fprintf(stderr, "FAIL: waited past its bound\n"); return 1; }
    int n = 3;
    w = gcm_flip_wait(countdown, &n, 100000);
    printf("ahead for 3 checks: waited %llu us\n", w);
    if (w >= 100000) { fprintf(stderr, "FAIL: kept waiting once the title was no longer ahead\n"); return 1; }
    w = gcm_flip_wait(countdown, &n, 100000);
    if (w != 0 && n >= 0) { fprintf(stderr, "FAIL: waited while the title was not ahead\n"); return 1; }
    puts("test_gcm_flip_wait: ok");
    return 0;
}
