/*
 * The CPU check: KZ1HD is compiled for an x86-64 level (CMakeLists.txt, KZ_MARCH; x86-64-v3 by default), so on an
 * older CPU its first AVX2 or BMI2 instruction stops it with SIGILL (Windows: an illegal instruction crash), with no
 * word on why. This runs first instead (a constructor at priority 101, before any C++ static initializer, which run
 * at the default 65535) and, when the CPU is below that level, says so and exits with KZ_EXIT_CPU (kz_exit.h):
 *   - one line "[kz] CPU check: ..." on stderr and, as main()'s log is not set up yet, written straight into the
 *     log file: KZ_LOG when set (the launcher sets it and shows the same message from the exit code), else on
 *     Windows kz1hd.log beside the exe when the exe has no stderr (started from Explorer; main.cpp's kz_log_setup);
 *   - on Windows a message box when started directly (no KZ_LOG and no stderr).
 * This file alone is compiled for baseline x86-64 (CMakeLists.txt), so nothing in it needs the level it checks;
 * tools/tests/test_cpu_check.sh checks that its object has no VEX-encoded instruction.
 * KZ_CPU_REQUIRED (2, 3 or 4) comes from KZ_MARCH; another KZ_MARCH (none, native, a CPU name) builds no check.
 * KZ_CPU_LEVEL_MAX=N (tests only) lowers the level found to N, to see the message on a CPU that has the level.
 * After Project Helghast's port/spu/kz3_cpu_check.c (undecuple, MIT); written here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_level.h"
#include "kz_exit.h"
#ifdef _WIN32
#include <windows.h>
#endif

#ifndef KZ_CPU_REQUIRED
#define KZ_CPU_REQUIRED 0
#endif

#ifdef _WIN32
static int kz_no_stderr(void)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    return !h || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN;
}
#endif

__attribute__((constructor(101))) static void kz_cpu_check(void)
{
    const int need = KZ_CPU_REQUIRED;
    if (need < 2) return;
    int have = kz_cpu_level();
    const char* max = getenv("KZ_CPU_LEVEL_MAX");
    if (max && *max >= '1' && *max <= '4' && max[1] == 0 && *max - '0' < have) have = *max - '0';
    if (have >= need) return;
    char msg[400];
    kz_cpu_message(msg, sizeof msg, have, need);
    fprintf(stderr, "[kz] CPU check: %s\n", msg);
    const char* log = getenv("KZ_LOG");
#ifdef _WIN32
    char file[MAX_PATH];
    const int direct = !(log && *log) && kz_no_stderr();
    if (direct) {   /* where kz_log_setup would have put the log */
        DWORD n = GetModuleFileNameA(NULL, file, MAX_PATH);
        char* slash = strrchr(file, '\\');
        if (n && n < MAX_PATH && slash && (size_t)(slash - file) + sizeof "\\kz1hd.log" <= MAX_PATH) {
            strcpy(slash, "\\kz1hd.log");
            log = file;
        }
    }
#endif
    if (log && *log) {
        FILE* f = fopen(log, "w");
        if (f) {
            fprintf(f, "[kz] CPU check: %s\n", msg);
            fclose(f);
        }
    }
#ifdef _WIN32
    if (direct) MessageBoxA(NULL, msg, "Project Vekta", MB_OK | MB_ICONERROR);
#endif
    fflush(stderr);
    _Exit(KZ_EXIT_CPU);
}
