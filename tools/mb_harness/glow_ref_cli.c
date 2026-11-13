/* glow_ref_cli: port/kz1hd/native/glow_ref.c on raw 4-byte-pixel files.
 *   texture <src 640x360> <out 640x360>              the glow texture (Modulate 1.0 .. the first Upsample)
 *   quarter <half 640x360> <out 640x360>             the same from the half-resolution copy (equal: Modulate 1.0 is a copy)
 *   add <full 1280x720> <glow 640x360> <out> [strength/256]
 *   all <full> <src 640x360> <out> [strength/256] */
#include "../../port/kz1hd/native/glow_ref.h"
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
static void save(const char* p, const uint8_t* b, size_t n)
{
    FILE* f = fopen(p, "wb");
    if (!f || fwrite(b, 1, n, f) != n) { perror(p); exit(1); }
    fclose(f);
}
int main(int argc, char** argv)
{
    const size_t F = GLOW_W * GLOW_H * 4, Hs = GLOW_HW * GLOW_HH * 4;
    const int s = argc >= 6 ? atoi(argv[5]) : GLOW_FULL;
    if (argc >= 4 && (!strcmp(argv[1], "texture") || !strcmp(argv[1], "quarter"))) {
        uint8_t *a = load(argv[2], Hs), *g = malloc(Hs); glow_ref_texture(a, g); save(argv[3], g, Hs); return 0; }
    if (argc >= 5 && !strcmp(argv[1], "add")) { uint8_t *a = load(argv[2], F), *g = load(argv[3], Hs); glow_ref_add(a, g, s); save(argv[4], a, F); return 0; }
    if (argc >= 5 && !strcmp(argv[1], "all")) { uint8_t *a = load(argv[2], F), *g = load(argv[3], Hs); glow_ref(a, g, s); save(argv[4], a, F); return 0; }
    fprintf(stderr, "usage: see the top of glow_ref_cli.c\n"); return 1;
}
