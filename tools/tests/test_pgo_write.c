/* The profile of an instrumented build at quit (port/kz1hd/pgo_write.h): kz_quit leaves through _exit, which skips
 * the profile runtime's atexit writer, so kz_pgo_write() must write it first. Built twice by test_pgo_write.sh:
 *   -DWITH_RUNTIME  this file defines __llvm_profile_write_file, as the profile runtime does in a KZ_PGO_GEN build
 *                   (a stand-in that writes a file): a child that calls kz_pgo_write() and then _exit leaves the
 *                   file, a child that only calls _exit (the old kz_quit) leaves none
 *   without it      no definition: the weak reference stays null, kz_pgo_write() does nothing and returns 0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "pgo_write.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

#ifdef WITH_RUNTIME
static char g_out[512];
static int g_writes;
/* the profile runtime's writer (compiler-rt InstrProfilingFile.c), here writing a marker with the call count */
int __llvm_profile_write_file(void)
{
    FILE* f = fopen(g_out, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", ++g_writes);
    fclose(f);
    return 0;
}
/* a counter the runtime would only write from its atexit handler, which _exit skips */
static void runtime_atexit(void) { __llvm_profile_write_file(); }

static int quit_child(int with_write)
{
    pid_t pid = fork();
    if (pid == 0) {
        atexit(runtime_atexit);
        if (with_write) kz_pgo_write();
        fflush(NULL);
        _exit(0);   /* as kz_quit */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}
#endif

int main(void)
{
#ifdef WITH_RUNTIME
    char dir[] = "/tmp/kzpgowriteXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    snprintf(g_out, sizeof g_out, "%s/kz.profraw", dir);
    CHECK(quit_child(0) && access(g_out, F_OK) != 0, "_exit alone (the old kz_quit): no profile written");
    CHECK(quit_child(1) && access(g_out, F_OK) == 0, "kz_pgo_write() then _exit: the profile is written");
    unlink(g_out);
    CHECK(kz_pgo_write() == 1 && access(g_out, F_OK) == 0, "with the runtime: kz_pgo_write() returns 1");
    CHECK(kz_pgo_write() == 1, "a second write (the KZ_PGO_DUMP timer, then the quit) works too");
    FILE* f = fopen(g_out, "r");
    int n = 0;
    if (f) { if (fscanf(f, "%d", &n) != 1) n = 0; fclose(f); }
    CHECK(n == 2, "the last write replaces the earlier one");
    unlink(g_out);
    rmdir(dir);
#else
    CHECK(__llvm_profile_write_file == NULL, "no profile runtime: the weak reference is null");
    CHECK(kz_pgo_write() == 0, "no profile runtime: kz_pgo_write() does nothing and returns 0");
#endif
    if (fails) return 1;
    puts("test_pgo_write: ok");
    return 0;
}
