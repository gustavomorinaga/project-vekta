/* Patch 4014: the EDAT cache goes to a port folder under a short fixed name, and nothing is written beside the source.
 *
 * Reuses ps3recomp's own synthetic EDAT builder (libs/filesystem/tests/test_edat_guest_klicensee.c: made-up
 * klicensee, content id and plaintext; no key or file from any title). Its main() is renamed away and the sceNp
 * symbols it names are stubbed: the key is registered with edat_add_klicensee, as sceNpDrmIsAvailable2 does.
 * Built and run by test_edat_cache.sh, which passes a scratch folder as argv[1]. */
#define main upstream_main
#include "tests/test_edat_guest_klicensee.c"
#undef main
#include <sys/stat.h>
#include <unistd.h>

uint8_t* vm_base;
s32 sceNpDrmIsAvailable(const u8* k, const char* p) { (void)k; (void)p; return 0; }
s32 sceNpDrmIsAvailable2(const u8* k, const char* p) { (void)k; (void)p; return 0; }

static int s_hook_calls;
static char s_hook_dec[1100];
static void hook(const char* src, const char* dec) { (void)src; s_hook_calls++; snprintf(s_hook_dec, sizeof s_hook_dec, "%s", dec); }

static int exists(const char* p) { struct stat st; return stat(p, &st) == 0; }
static long fsize(const char* p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }
static int count_entries(const char* dir)
{
    char cmd[1200]; snprintf(cmd, sizeof cmd, "ls -A '%s' | wc -l", dir);
    FILE* f = popen(cmd, "r"); int n = -1;
    if (f) { if (fscanf(f, "%d", &n) != 1) n = -1; pclose(f); }
    return n;
}
static int matches(const char* path, const uint8_t* plain)
{
    static uint8_t got[FSIZE + 16];
    FILE* f = fopen(path, "rb");
    size_t n = f ? fread(got, 1, sizeof got, f) : 0;
    if (f) fclose(f);
    return n == FSIZE && memcmp(got, plain, FSIZE) == 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) { puts("FAIL: usage: test_edat_cache SCRATCH_DIR"); return 1; }
    char src_dir[1024], cache[1024], in[1100], beside[1100], buf[1100], keyf[1200], what[1400];
    snprintf(src_dir, sizeof src_dir, "%s/game", argv[1]);
    snprintf(cache, sizeof cache, "%s/work/edat-cache", argv[1]);   /* not created: edat makes it */
    mkdir(src_dir, 0755);
    snprintf(in, sizeof in, "%s/data_ps3.psarc.edat", src_dir);
    snprintf(beside, sizeof beside, "%s.dec", in);

    static uint8_t plain[FSIZE];
    for (uint32_t i = 0; i < FSIZE; i++) plain[i] = (uint8_t)(i * 7 + (i >> 8));
    if (build_edat(in, plain) != 0) { puts("FAIL: could not write the fixture"); return 1; }
    chmod(src_dir, 0555);   /* a game install: read-only */

    edat_set_cache_dir(cache);
    edat_set_ready_hook(hook);

    const char* r = edat_resolve(in, buf, sizeof buf);
    check(r == in, "before the title's klicensee: the source is handed back");
    check(!exists(beside), "before the klicensee: nothing beside the source");
    snprintf(what, sizeof what, "before the klicensee: no .part or .dec left in the cache (%d entries)", count_entries(cache));
    check(count_entries(cache) <= 0, what);
    check(s_hook_calls == 0, "before the klicensee: the ready hook is not called");

    edat_add_klicensee(KLIC);
    r = edat_resolve(in, buf, sizeof buf);
    snprintf(what, sizeof what, "with the klicensee: a cache path (got %s)", r);
    check(r == buf && strncmp(r, cache, strlen(cache)) == 0, what);
    const char* name = r + strlen(cache) + 1;
    snprintf(what, sizeof what, "the cache name is 16 hex digits + \".dec\" (got %s)", name);
    check(strlen(name) == 20 && strspn(name, "0123456789abcdef") == 16 && strcmp(name + 16, ".dec") == 0, what);
    check(matches(r, plain), "the cached plaintext matches");
    snprintf(keyf, sizeof keyf, "%s.key", r);
    check(fsize(keyf) == 0x38, "the .key sidecar is written (0x38 bytes)");
    check(!exists(beside), "nothing beside the source");
    check(s_hook_calls == 1 && strcmp(s_hook_dec, r) == 0, "the ready hook gets the cache path");

    char dec[1100]; snprintf(dec, sizeof dec, "%s", r);
    FILE* f = fopen(dec, "r+b"); if (f) { fputc(0xEE, f); fclose(f); }   /* same size, marked */
    r = edat_resolve(in, buf, sizeof buf);
    f = fopen(dec, "rb"); int c = f ? fgetc(f) : -1; if (f) fclose(f);
    check(r == buf && strcmp(r, dec) == 0 && c == 0xEE, "second open: the cache is reused, not decrypted again");
    check(s_hook_calls == 2, "second open: the ready hook is called again");

    f = fopen(keyf, "r+b"); if (f) { fputc(0x00, f); fputc(0x00, f); fclose(f); }   /* another source's header */
    r = edat_resolve(in, buf, sizeof buf);
    check(r == buf && matches(dec, plain), "sidecar that does not match: decrypted again");

    if (truncate(dec, 100) != 0) check(0, "truncate");
    r = edat_resolve(in, buf, sizeof buf);
    check(r == buf && matches(dec, plain), "cache of the wrong size: decrypted again");

    char other[1024]; snprintf(other, sizeof other, "%s/env-cache", argv[1]);
    setenv("PS3_EDAT_CACHE", other, 1);
    r = edat_resolve(in, buf, sizeof buf);
    snprintf(what, sizeof what, "$PS3_EDAT_CACHE wins over edat_set_cache_dir (got %s)", r);
    check(r == buf && strncmp(r, other, strlen(other)) == 0 && matches(r, plain), what);
    unsetenv("PS3_EDAT_CACHE");

    edat_set_cache_dir(NULL);
    chmod(src_dir, 0755);
    r = edat_resolve(in, buf, sizeof buf);
    check(r == buf && strcmp(r, beside) == 0 && matches(beside, plain), "no cache folder: the old \"<file>.dec\" beside the source");

    if (s_failed) { printf("FAIL (%d failed)\n", s_failed); return 1; }
    puts("PASS (0 failed)");
    return 0;
}
