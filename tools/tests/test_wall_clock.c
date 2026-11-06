/* LV2 145's clock (patch 3032): lv2_wall_clock() is the host's wall clock, seconds since 1970-01-01 UTC, on Linux
 * and on Windows (tools/wine-host-tests.sh runs this built for Windows under Wine). Before the patch the Windows
 * build returned the time since boot. */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "lv2_wall_clock.h"

int main(void)
{
    uint64_t sec = 0, nsec = 0;
    lv2_wall_clock(&sec, &nsec);
    int64_t host = (int64_t)time(NULL);
    printf("lv2_wall_clock: %llu.%09llu, time(): %lld\n", (unsigned long long)sec, (unsigned long long)nsec,
           (long long)host);
    if (nsec >= 1000000000ULL) { fprintf(stderr, "FAIL: nanoseconds out of range\n"); return 1; }
    if (sec < 1700000000ULL) { fprintf(stderr, "FAIL: before November 2023, not the wall clock\n"); return 1; }
    int64_t d = (int64_t)sec - host;
    if (d < -5 || d > 5) { fprintf(stderr, "FAIL: %lld s from time()\n", (long long)d); return 1; }
    puts("test_wall_clock: ok");
    return 0;
}
