/* The Vulkan backend's disk caches (patch 2099, rsx_vk_disk_cache.h): the folder on Windows, where no HOME is set
 * (before the patch both caches were off there), a cache file replaced over an existing one (rename() fails on
 * Windows then), and the pipelines.bin header check. Built for Linux by test_vk_disk_cache.sh and for Windows by
 * tools/wine-host-tests.sh. Writes only in its own temporary folder. */
#include "rsx_vk_disk_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void put(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "FAIL: cannot write %s\n", path); exit(1); }
    fputs(text, f);
    fclose(f);
}

int main(int argc, char** argv)
{
    char d[512], p[512];
    /* Windows: no HOME, LOCALAPPDATA set (what a player's Windows has) */
    CHECK(rsx_vk_cache_dir_pick(d, sizeof d, p, sizeof p, 1, NULL, NULL, "D:\\LocalAppData"));
    CHECK(strcmp(d, "D:\\LocalAppData/ps3recomp-vk") == 0 && p[0] == 0);
    CHECK(rsx_vk_cache_dir_pick(d, sizeof d, p, sizeof p, 1, "", NULL, "L") && strcmp(d, "L/ps3recomp-vk") == 0);
    /* the port's folder ($PS3RECOMP_VK_CACHE) wins everywhere; "0" is off */
    CHECK(rsx_vk_cache_dir_pick(d, sizeof d, p, sizeof p, 1, "work/vk-cache", "/h", "L") && strcmp(d, "work/vk-cache") == 0);
    CHECK(!rsx_vk_cache_dir_pick(d, sizeof d, p, sizeof p, 1, "0", "/h", "L"));
    CHECK(!rsx_vk_cache_dir_pick(d, sizeof d, p, sizeof p, 0, "0", "/h", NULL));
    /* Linux as before: ~/.cache/ps3recomp-vk, ~/.cache made first; LOCALAPPDATA ignored */
    CHECK(rsx_vk_cache_dir_pick(d, sizeof d, p, sizeof p, 0, NULL, "/h", "L"));
    CHECK(strcmp(d, "/h/.cache/ps3recomp-vk") == 0 && strcmp(p, "/h/.cache") == 0);
    CHECK(!rsx_vk_cache_dir_pick(d, sizeof d, p, sizeof p, 0, NULL, NULL, NULL));

    /* a save over an existing file replaces it */
    const char* dir = argc > 1 ? argv[1] : ".";
    char a[600], b[600];
    snprintf(a, sizeof a, "%s/pipelines.bin.tmp", dir);
    snprintf(b, sizeof b, "%s/pipelines.bin", dir);
    put(b, "old");
    put(a, "new");
    CHECK(rsx_vk_replace_file(a, b) == 0);
    char got[8] = "";
    FILE* f = fopen(b, "rb");
    CHECK(f != NULL);
    if (f) { got[fread(got, 1, sizeof got - 1, f)] = 0; fclose(f); }
    CHECK(strcmp(got, "new") == 0);
    f = fopen(a, "rb");
    CHECK(f == NULL);
    if (f) fclose(f);
    remove(b);

    /* pipelines.bin header: VkPipelineCacheHeaderVersionOne, least significant byte first */
    uint8_t uuid[16], h[48] = {0};
    for (int i = 0; i < 16; i++) uuid[i] = (uint8_t)(0xA0 + i);
    h[0] = 32; h[4] = 1;
    h[8] = 0x02; h[9] = 0x10;              /* vendor 0x1002 */
    h[12] = 0xBF; h[13] = 0x73;            /* device 0x73BF */
    memcpy(h + 16, uuid, 16);
    CHECK(rsx_vk_pcache_header_ok(h, sizeof h, 0x1002, 0x73BF, uuid));
    CHECK(rsx_vk_pcache_header_ok(h, 32, 0x1002, 0x73BF, uuid));
    CHECK(!rsx_vk_pcache_header_ok(h, 31, 0x1002, 0x73BF, uuid));          /* torn */
    CHECK(!rsx_vk_pcache_header_ok(NULL, 0, 0x1002, 0x73BF, uuid));
    CHECK(!rsx_vk_pcache_header_ok(h, sizeof h, 0x10DE, 0x73BF, uuid));    /* another vendor */
    CHECK(!rsx_vk_pcache_header_ok(h, sizeof h, 0x1002, 0x73DF, uuid));    /* another device */
    uuid[15] ^= 1;
    CHECK(!rsx_vk_pcache_header_ok(h, sizeof h, 0x1002, 0x73BF, uuid));    /* another driver */
    uuid[15] ^= 1;
    h[4] = 2;
    CHECK(!rsx_vk_pcache_header_ok(h, sizeof h, 0x1002, 0x73BF, uuid));    /* another header version */
    h[4] = 1; h[0] = 64;
    CHECK(!rsx_vk_pcache_header_ok(h, sizeof h, 0x1002, 0x73BF, uuid));    /* header longer than the file */
    memset(h, 0xFF, sizeof h);
    CHECK(!rsx_vk_pcache_header_ok(h, sizeof h, 0x1002, 0x73BF, uuid));    /* garbage */

    printf("test_vk_disk_cache: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
