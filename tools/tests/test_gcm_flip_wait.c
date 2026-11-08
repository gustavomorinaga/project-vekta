/* PS3_GCM_MAX_QUEUED_FLIPS's wait (patch 2055): gcm_flip_wait() really waits, up to its bound, while the title is
 * ahead, and stops as soon as it is not, on Linux and on Windows (tools/wine-host-tests.sh runs this built for
 * Windows under Wine). Before the patch the Windows wait was 1000 rounds of Sleep(0), over within microseconds. */
#include <stdio.h>
#include "gcm_flip_wait.h"

static int always(void* arg) { (void)arg; return 1; }
static int countdown(void* arg) { int* n = (int*)arg; return (*n)-- > 0; }
static int until(void* arg) { return gcm_flip_wait_now_us() < *(unsigned long long*)arg; }

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
    if (!gcm_flip_wait_hires()) { fprintf(stderr, "FAIL: no high-resolution timer for the naps\n"); return 1; }
    /* ahead for 2 to 4.6 ms, 20 times: how long past the moment it stopped being ahead did each wait end (with
     * Sleep(1) naps, about half a millisecond on average) */
    unsigned long long late = 0;
    for (int i = 0; i < 20; i++) {
        unsigned long long end = gcm_flip_wait_now_us() + 2000 + 137 * (unsigned)i;
        gcm_flip_wait(until, &end, 100000);
        late += gcm_flip_wait_now_us() - end;
    }
    printf("ahead for 2 to 4.6 ms: the waits ended %llu us late on average\n", late / 20);
#ifdef _WIN32
    /* How fine the timer is depends on the system: 100 us naps take about 108 us on Windows 10 1803 and later
     * and under Wine 11, but a millisecond under Wine 9 (CI's Ubuntu package), the same as Sleep(1). So on
     * Windows the naps must be no longer than Sleep(1)'s, measured here, and are only reported. */
    unsigned long long t0 = gcm_flip_wait_now_us();
    for (int i = 0; i < 50; i++) gcm_flip_wait_nap();
    const unsigned long long nap = (gcm_flip_wait_now_us() - t0) / 50;
    t0 = gcm_flip_wait_now_us();
    for (int i = 0; i < 50; i++) Sleep(1);
    const unsigned long long slp = (gcm_flip_wait_now_us() - t0) / 50;
    printf("a nap: %llu us; Sleep(1): %llu us\n", nap, slp);
    if (nap > slp + slp / 10 + 50) { fprintf(stderr, "FAIL: the naps are longer than Sleep(1)'s\n"); return 1; }
#else
    if (late / 20 > 300) { fprintf(stderr, "FAIL: the naps are too long\n"); return 1; }
#endif
    puts("test_gcm_flip_wait: ok");
    return 0;
}
