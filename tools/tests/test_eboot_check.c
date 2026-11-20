/* Host test for port/kz1hd/eboot_check.h: its SHA-256 against Python's hashlib on the FIPS 180-4 padding edges (empty,
 * "abc", 55, 56 and 64 bytes) and 1 MB fed in uneven pieces; then kz_eboot_check() on files in a folder with a space
 * and accented letters in its name (UTF-8 on Linux, the ANSI code page under Wine, as fopen and ppu_load_elf see
 * argv): the same file is equal, one changed byte or a truncated file is different, a missing file is unreadable.
 * Built and run by tools/tests/test_eboot_check.sh (Linux) and tools/wine-host-tests.sh (Windows). Arguments: the
 * folder to work in. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eboot_check.h"

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); fails += !ok_; printf(ok_ ? "ok   " : "FAIL "); printf(__VA_ARGS__); printf("\n"); } while (0)

static void hex_of(const void* p, size_t n, size_t piece, char out[65])
{
    kz_sha256 s;
    kz_sha256_init(&s);
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i += piece) kz_sha256_update(&s, b + i, n - i < piece ? n - i : piece);
    kz_sha256_hex(&s, out);
}

static int write_file(const char* path, const unsigned char* p, size_t n)
{
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t w = fwrite(p, 1, n, f);
    return fclose(f) == 0 && w == n;
}

int main(int argc, char** argv)
{
    /* hashlib.sha256(...).hexdigest() */
    static const struct { const char* name; size_t n; char fill; const char* want; } V[] = {
        {"empty", 0, 'a', "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"55 bytes (one block with the length)", 55, 'a', "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {"56 bytes (the length needs a second block)", 56, 'a', "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {"64 bytes (one whole block)", 64, 'a', "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    };
    char got[65];
    unsigned char a[64];
    memset(a, 'a', sizeof a);
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        hex_of(a, V[i].n, 64, got);
        CHECK(!strcmp(got, V[i].want), "SHA-256 of %s", V[i].name);
    }
    hex_of("abc", 3, 1, got);
    CHECK(!strcmp(got, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 of \"abc\" (FIPS 180-4's example)");
    enum { MB = 1 << 20 };
    unsigned char* big = (unsigned char*)malloc(MB);
    for (size_t i = 0; i < MB; i++) big[i] = (unsigned char)(i * 7 + 3);
    const char* big_want = "172c15dc2e12b50e523d8e657cbe7fbb11c1053252bbf1e1431077d57d8128fd";
    hex_of(big, MB, 1000003 % 4093, got);
    CHECK(!strcmp(got, big_want), "SHA-256 of 1 MB fed in pieces of %d bytes", 1000003 % 4093);
    hex_of(big, MB, MB, got);
    CHECK(!strcmp(got, big_want), "and in one piece");

    if (argc < 2) { printf("FAIL no folder given\n"); return 1; }
    char path[1024];
    snprintf(path, sizeof path, "%s/EBOOT.elf", argv[1]);
    CHECK(write_file(path, big, MB), "a 1 MB stand-in EBOOT is written into %s", argv[1]);
    long long size;
    char hex[65];
    CHECK(kz_eboot_check(path, big_want, MB, &size, hex) == KZ_EBOOT_EQUAL && size == MB && !strcmp(hex, big_want),
          "the same file: equal");
    big[MB / 2] ^= 1;
    write_file(path, big, MB);
    CHECK(kz_eboot_check(path, big_want, MB, &size, hex) == KZ_EBOOT_DIFFERENT && hex[0] && strcmp(hex, big_want),
          "one byte changed: different, with its own hash");
    write_file(path, big, MB - 1);
    CHECK(kz_eboot_check(path, big_want, MB, &size, hex) == KZ_EBOOT_DIFFERENT && size == MB - 1 && !hex[0],
          "truncated: different by its size, not hashed");
    snprintf(path, sizeof path, "%s/none.elf", argv[1]);
    CHECK(kz_eboot_check(path, big_want, MB, &size, hex) == KZ_EBOOT_UNREADABLE && size == -1, "missing: unreadable");
    free(big);
    printf("test_eboot_check: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
