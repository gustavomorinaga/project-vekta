/*
 * The game's FIOS cache (/dev_hdd1/cache/BCUS98116/fios: cache.dat, a 512-byte
 * file that starts "fioscach", and cache.idx). The game's own FIOS code reads
 * the header at start; a cache.dat cut short (empty when the first run was
 * killed while writing it) is never rewritten: FIOS reopens both files about
 * 6,000 times a second, logs "failed reading cache header" and the game never
 * draws a frame (2026-11-24, VADER; the hang watchdog does not see it, as it
 * arms at the first frame). A missing cache.dat beside a cache.idx does the
 * same (FIOS makes an empty cache.dat and loops); with neither file the game
 * makes both again, the same bytes, with no slower start. So before ppu_run
 * the port checks cache.dat's 512-byte header and, when it is missing or not
 * one, removes both files, as on a first run. Header-only, plain C, for a host test
 * (tools/tests/test_fios_cache.c).
 */
#ifndef KZ_FIOS_CACHE_H
#define KZ_FIOS_CACHE_H
#include <stdio.h>
#include <string.h>

#define KZ_FIOS_CACHE_MAGIC "fioscach"

#define KZ_FIOS_CACHE_HEADER 512

/* 1 when there is nothing to do: neither file (a first run), or a cache.dat
 * with its whole header. 0 otherwise (cache.dat missing beside a cache.idx,
 * shorter than its header, or not starting with the magic): then
 * dir/cache.dat and dir/cache.idx are removed (a missing one is fine). */
static inline int kz_fios_cache_check(const char* dir)
{
    char dat[1024], idx[1024], head[KZ_FIOS_CACHE_HEADER];
    snprintf(dat, sizeof dat, "%s/cache.dat", dir);
    snprintf(idx, sizeof idx, "%s/cache.idx", dir);
    FILE* f = fopen(dat, "rb");
    if (!f) {
        FILE* i = fopen(idx, "rb");
        if (!i) return 1;
        fclose(i);
        remove(idx);
        return 0;
    }
    const size_t n = fread(head, 1, sizeof head, f);
    fclose(f);
    if (n == sizeof head && memcmp(head, KZ_FIOS_CACHE_MAGIC, 8) == 0) return 1;
    remove(dat);
    remove(idx);
    return 0;
}
#endif
