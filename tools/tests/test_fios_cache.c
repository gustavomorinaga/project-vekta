/* fios_cache.h: a FIOS cache.dat without its 512-byte "fioscach" header (empty after a killed first run, or cut
 * short), or missing beside a cache.idx, is removed with cache.idx so the game makes a new cache; a good one, or
 * none, is left alone. kz_fios_cache_check_all does the same in every <id>/fios of /dev_hdd1/cache: the id is the
 * edition's TITLE_ID (BCUS98116, BCES01743). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fios_cache.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

static void put(const char* dir, const char* name, const void* data, size_t n)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE* f = fopen(p, "wb");
    if (n) fwrite(data, 1, n, f);
    fclose(f);
}

static int has(const char* dir, const char* name)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE* f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    const char* d = argv[1];
    char good[512] = "fioscach";
    CHECK(kz_fios_cache_check(d) == 1, "no cache.dat: nothing to do");
    put(d, "cache.dat", good, sizeof good); put(d, "cache.idx", "x", 1);
    CHECK(kz_fios_cache_check(d) == 1 && has(d, "cache.dat") && has(d, "cache.idx"), "a good cache is kept");
    put(d, "cache.dat", "", 0);
    CHECK(kz_fios_cache_check(d) == 0 && !has(d, "cache.dat") && !has(d, "cache.idx"),
          "an empty cache.dat is removed with cache.idx");
    put(d, "cache.dat", "fios", 4); put(d, "cache.idx", "x", 1);
    CHECK(kz_fios_cache_check(d) == 0 && !has(d, "cache.dat") && !has(d, "cache.idx"), "a cut-short header is removed");
    put(d, "cache.dat", "notfios!", 8);
    CHECK(kz_fios_cache_check(d) == 0 && !has(d, "cache.dat"), "a wrong header is removed (no cache.idx is fine)");
    put(d, "cache.dat", good, 100); put(d, "cache.idx", "x", 1);
    CHECK(kz_fios_cache_check(d) == 0 && !has(d, "cache.dat") && !has(d, "cache.idx"),
          "the magic with less than the 512-byte header is removed");
    put(d, "cache.idx", "x", 1);
    CHECK(kz_fios_cache_check(d) == 0 && !has(d, "cache.idx"),
          "a cache.idx with no cache.dat is removed (FIOS loops on that too)");
    /* every edition's cache: the European disc's cut short, the US one good, a folder with no fios */
    const char* c = argv[2];
    char us[1024], eu[1024];
    snprintf(us, sizeof us, "%s/BCUS98116/fios", c);
    snprintf(eu, sizeof eu, "%s/BCES01743/fios", c);
    put(us, "cache.dat", good, sizeof good); put(us, "cache.idx", "x", 1);
    put(eu, "cache.dat", "", 0); put(eu, "cache.idx", "x", 1);
    CHECK(kz_fios_cache_check_all(c, NULL) == 1, "one cache of the two is cut short");
    CHECK(!has(eu, "cache.dat") && !has(eu, "cache.idx"), "the European disc's cut-short cache is removed");
    CHECK(has(us, "cache.dat") && has(us, "cache.idx"), "the US edition's good cache is kept");
    CHECK(kz_fios_cache_check_all(c, NULL) == 0, "nothing left to remove");
    snprintf(eu, sizeof eu, "%s/missing", c);
    CHECK(kz_fios_cache_check_all(eu, NULL) == 0, "no /dev_hdd1/cache yet (a first run)");
    printf("test_fios_cache: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
