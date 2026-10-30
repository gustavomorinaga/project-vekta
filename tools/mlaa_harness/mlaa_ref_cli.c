/* mlaa_ref_cli <in.argb> <out.argb>: port/kz1hd/native/mlaa_ref.c on a raw 1280x720 ARGB image (768 rows allowed: the first 720 are used) */
#include "../../port/kz1hd/native/mlaa_ref.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv)
{
    if (argc != 3) { fprintf(stderr, "usage: mlaa_ref_cli in.argb out.argb\n"); return 1; }
    uint8_t* img = calloc(MLAA_W * 768, 4);
    FILE* f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(img, 4, MLAA_W * 768, f); fclose(f);
    if (n < MLAA_W * MLAA_H) { fprintf(stderr, "short image\n"); return 1; }
    mlaa_ref(img);
    f = fopen(argv[2], "wb"); fwrite(img, 4, MLAA_W * MLAA_H, f); fclose(f);
    return 0;
}
