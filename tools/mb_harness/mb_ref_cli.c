/* mb_ref_cli: port/kz1hd/native/mb_ref.c, one stage at a time, on raw ARGB files.
 *   field <in 640x360> <out>          gather <half> <field> <out>
 *   down <in 1280x720> <out 640x360>  blend <full> <result 640x360> <out>
 *   all <full> <rsx field> <out> */
#include "../../port/kz1hd/native/mb_ref.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t* load(const char* p, size_t n)
{
    uint8_t* b = calloc(n, 1); FILE* f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    if (fread(b, 1, n, f) != n) { fprintf(stderr, "%s: short\n", p); exit(1); }
    fclose(f); return b;
}
static void save(const char* p, const uint8_t* b, size_t n) { FILE* f = fopen(p, "wb"); fwrite(b, 1, n, f); fclose(f); }
int main(int argc, char** argv)
{
    const size_t F = MB_W * MB_H * 4, Hs = MB_HW * MB_HH * 4;
    if (argc >= 4 && !strcmp(argv[1], "field")) { uint8_t* a = load(argv[2], Hs); mb_ref_field(a); save(argv[3], a, Hs); return 0; }
    if (argc >= 4 && !strcmp(argv[1], "down")) { uint8_t* a = load(argv[2], F); uint8_t* h = malloc(Hs); mb_ref_down(a, h); save(argv[3], h, Hs); return 0; }
    if (argc >= 5 && !strcmp(argv[1], "gather")) {
        uint8_t *h = load(argv[2], Hs), *f = load(argv[3], Hs);
        mb_ref_gather(h, f);
        save(argv[4], f, Hs); return 0; }
    if (argc >= 5 && !strcmp(argv[1], "blend")) { uint8_t *a = load(argv[2], F), *r = load(argv[3], Hs); mb_ref_blend(a, r); save(argv[4], a, F); return 0; }
    if (argc >= 5 && !strcmp(argv[1], "all")) { uint8_t *a = load(argv[2], F), *r = load(argv[3], Hs); mb_ref(a, r); save(argv[4], a, F); return 0; }
    fprintf(stderr, "usage: see the top of mb_ref_cli.c\n"); return 1;
}
