/* mlaa_ref_cli <in.argb> <out.argb> [W H]: port/kz1hd/native/mlaa_ref.c on a raw ARGB image, 1280x720 by default
 * (768 rows allowed then: the first 720 are used), else W x H (mlaa_ref_wh) */
#include "../../port/kz1hd/native/mlaa_ref.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv)
{
    if (argc != 3 && argc != 5) { fprintf(stderr, "usage: mlaa_ref_cli in.argb out.argb [W H]\n"); return 1; }
    const int w = argc == 5 ? atoi(argv[3]) : MLAA_W, h = argc == 5 ? atoi(argv[4]) : MLAA_H;
    const size_t rows = argc == 5 ? (size_t)h : 768;
    uint8_t* img = calloc((size_t)w * rows, 4);
    FILE* f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(img, 4, (size_t)w * rows, f); fclose(f);
    if (n < (size_t)w * h) { fprintf(stderr, "short image\n"); return 1; }
    if (argc == 5) mlaa_ref_wh(img, w, h); else mlaa_ref(img);
    f = fopen(argv[2], "wb"); fwrite(img, 4, (size_t)w * h, f); fclose(f);
    return 0;
}
