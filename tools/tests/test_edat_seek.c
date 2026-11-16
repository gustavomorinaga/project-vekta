/* Patch 4015: the EDAT reader seeks with 64-bit offsets. fseek takes a long, 32 bits on Windows, so every block of a
 * data archive past 2 GiB (Killzone HD's is 3 GB) failed to read there and nothing was decrypted. This seeks to 2.5 GiB
 * in a sparse 3 GiB file and reads a marker back. Linux: tools/tests/test_edat_seek.sh; Windows: tools/wine-host-tests.sh
 * (the case that failed). Writes only to the folder given as argv[1] (default: the current one). */
#include "../edat.c"

int main(int argc, char** argv)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/edat_seek_test.bin", argc > 1 ? argv[1] : ".");
    const uint64_t at = 0xA0000000ull;   /* 2.5 GiB */
    const char mark[] = "past-two-gib";
    FILE* f = fopen(path, "wb");
    if (!f) { puts("FAIL: cannot write the sparse file"); return 1; }
    int ok = edat_seek(f, at) == 0 && fwrite(mark, 1, sizeof mark, f) == sizeof mark;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(path); puts("FAIL: could not write past 2 GiB"); return 1; }
    char got[sizeof mark] = {0};
    f = fopen(path, "rb");
    ok = f && edat_seek(f, at) == 0 && fread(got, 1, sizeof got, f) == sizeof got && memcmp(got, mark, sizeof mark) == 0;
    if (f) fclose(f);
    remove(path);
    puts(ok ? "PASS: edat_seek reads at 2.5 GiB" : "FAIL: edat_seek did not reach 2.5 GiB");
    return ok ? 0 : 1;
}
