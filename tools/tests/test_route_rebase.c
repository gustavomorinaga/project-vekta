/* The route file writer (port/kz1hd/route_rebase.h) that recordings (KZ_ROUTE_REC) and rebased replays
 * (KZ_ROUTE_REBASE) share: the exact lines route.cpp's load() reads back (H, F with both generators, P with "=" for a
 * repeated pad, M only when the mouse moved, S with the position bits and the generator), and the rebase's note
 * as a comment the loader skips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "route_rebase.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

int main(void)
{
    char dir[] = "/tmp/kzrouterebaseXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char path[256];
    snprintf(path, sizeof path, "%s/r.route", dir);
    struct kz_route_writer w;
    memset(&w, 0, sizeof w);
    w.f = fopen(path, "w");
    if (!w.f) { perror("fopen"); return 1; }

    uint8_t pad[KZ_ROUTE_PAD_BYTES], pad2[KZ_ROUTE_PAD_BYTES];
    for (int i = 0; i < KZ_ROUTE_PAD_BYTES; i++) { pad[i] = (uint8_t)i; pad2[i] = 0; }
    pad2[16] = 0xbf; pad2[17] = 0x80;
    const uint32_t pos[3] = {0x42200000u, 0x41d60000u, 0x3ed99999u};

    kz_route_w_header(&w, "/r/new.route", "rebased (KZ_ROUTE_REBASE) from a loose replay of /r/old.route", 144,
                      0x91722c16u, 0x00024dc2u);
    kz_route_w_pad(&w, 1, pad);
    kz_route_w_pad(&w, 1, pad);      /* the same bytes: "=" */
    kz_route_w_frame(&w, 1, 25, 24, 0.0, 0x7fbb18f5u, 0x00024dc2u);
    kz_route_w_mouse(&w, 0, 0);      /* not moved: no line */
    kz_route_w_mouse(&w, -3, 7);
    kz_route_w_pos(&w, pos, 0xfad2d226u);
    kz_route_w_pad(&w, 0, pad2);     /* other bytes: written out, and the new "last" */
    kz_route_w_pad(&w, 0, pad2);
    kz_route_w_frame(&w, 2, 27, 25, 13.875, 0x12345678u, 0xabcdef01u);
    fclose(w.f);

    const char* want =
        "# kz-route 1: KZ_ROUTE_PLAY=/r/new.route replays it from the same start\n"
        "# rebased (KZ_ROUTE_REBASE) from a loose replay of /r/old.route\n"
        "H tick_hz 144\nH rng 91722c16\nH rng2 00024dc2\n"
        "P 1 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223\n"
        "P 1 =\n"
        "F 1 25 24 0.00 7fbb18f5 00024dc2\n"
        "M -3 7\n"
        "S 42200000 41d60000 3ed99999 fad2d226\n"
        "P 0 00000000000000000000000000000000bf80000000000000000000000000000000000000\n"
        "P 0 =\n"
        "F 2 27 25 13.88 12345678 abcdef01\n";
    char got[2048] = {0};
    FILE* f = fopen(path, "r");
    size_t n = f ? fread(got, 1, sizeof got - 1, f) : 0;
    if (f) fclose(f);
    got[n] = 0;
    CHECK(!strcmp(got, want), "the lines are exactly what route.cpp's load() reads");
    if (strcmp(got, want)) fprintf(stderr, "--- got:\n%s--- want:\n%s", got, want);

    /* a writer without a note: no comment line beyond the first */
    memset(&w, 0, sizeof w);
    w.f = fopen(path, "w");
    kz_route_w_header(&w, "/r/rec.route", NULL, 60, 1, 2);
    fclose(w.f);
    f = fopen(path, "r");
    n = f ? fread(got, 1, sizeof got - 1, f) : 0;
    if (f) fclose(f);
    got[n] = 0;
    CHECK(!strcmp(got, "# kz-route 1: KZ_ROUTE_PLAY=/r/rec.route replays it from the same start\n"
                       "H tick_hz 60\nH rng 00000001\nH rng2 00000002\n"), "a recording's header: one comment, three H lines");

    unlink(path);
    rmdir(dir);
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    return 0;
}
