/*
 * The heap in use, for the stats line: glibc's allocator count, or the process's private bytes on Windows. mallinfo2
 * is glibc 2.33 and later; the Linux release folder builds with the pinned Zig against glibc 2.31 (port/cmake/
 * zig-linux.cmake), which has only mallinfo, whose int fields wrap past 2 GB (enough for a stats line). Header only,
 * so tools/tests/test_heap_bytes.sh links it against glibc 2.31.
 */
#ifndef KZ_HEAP_BYTES_H
#define KZ_HEAP_BYTES_H
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <malloc.h>
#endif

static inline size_t kz_heap_bytes(void)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    return GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc) ? pmc.PrivateUsage : 0;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return mallinfo2().uordblks;
#elif defined(__GLIBC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   /* the only call there is before 2.33 */
    return (size_t)(unsigned)mallinfo().uordblks;
#pragma GCC diagnostic pop
#else
    return 0;   /* another C library: no count */
#endif
}
#endif
