/* Patch 2082: the GPU line the Vulkan backend logs at start (rsx_vk_driver.h). driverVersion is packed differently by
 * NVIDIA (10.8.8.6 bits), by Intel's Windows driver (18.14) and by everyone else (VK_MAKE_VERSION, 10.10.12): a bug
 * report must show the version the vendor's own tools show. Also the vendor names and the VRAM figure (the largest
 * device-local heap). */
#include <stdio.h>
#include <string.h>
#include "rsx_vk_driver.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

static void expect(uint32_t vendor, uint32_t id, uint32_t v, int win, const char* want)
{
    char b[48];
    rsx_vk_driver_version_text(b, sizeof b, vendor, id, v, win);
    CHECK(!strcmp(b, want), "vendor 0x%X id %u version 0x%08X: '%s', want '%s'", vendor, id, v, b, want);
}

#define STD(ma, mi, pa) (((uint32_t)(ma) << 22) | ((uint32_t)(mi) << 12) | (uint32_t)(pa))

int main(void)
{
    /* NVIDIA: 566.36 (Windows and Linux alike), the branches only when set */
    expect(RSX_VK_VENDOR_NVIDIA, 0, (566u << 22) | (36u << 14), 1, "566.36");
    expect(RSX_VK_VENDOR_NVIDIA, 4, (550u << 22) | (7u << 14), 0, "550.07");
    expect(RSX_VK_VENDOR_NVIDIA, 4, (535u << 22) | (183u << 14) | (1u << 6) | 2u, 0, "535.183.1.2");
    /* Intel's Windows driver: 101.6130; known by the driver id, or by the platform when the id is not known */
    expect(RSX_VK_VENDOR_INTEL, RSX_VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, (101u << 14) | 6130u, 0, "101.6130");
    expect(RSX_VK_VENDOR_INTEL, 0, (101u << 14) | 6130u, 1, "101.6130");
    /* Intel's Mesa driver (id 6) and an unknown id on Linux: the standard packing */
    expect(RSX_VK_VENDOR_INTEL, 6, STD(24, 2, 8), 1, "24.2.8");
    expect(RSX_VK_VENDOR_INTEL, 0, STD(24, 2, 8), 0, "24.2.8");
    /* AMD (AMDVLK, AMD's Windows driver, RADV) and others: VK_MAKE_VERSION */
    expect(RSX_VK_VENDOR_AMD, 1, STD(2, 0, 302), 1, "2.0.302");
    expect(RSX_VK_VENDOR_AMD, 3, STD(24, 3, 4095), 0, "24.3.4095");
    expect(0x10005u, 13, STD(0, 0, 1), 0, "0.0.1");

    /* the text is always terminated, however small the buffer */
    char tiny[4] = "xyz";
    rsx_vk_driver_version_text(tiny, sizeof tiny, RSX_VK_VENDOR_NVIDIA, 0, (566u << 22) | (36u << 14), 1);
    CHECK(!strcmp(tiny, "566"), "a short buffer holds the start, terminated: '%s'", tiny);
    CHECK(rsx_vk_driver_version_text(NULL, 8, 0, 0, 0, 0) == NULL, "no buffer: nothing written");

    CHECK(!strcmp(rsx_vk_vendor_name(0x1002u), "AMD") && !strcmp(rsx_vk_vendor_name(0x10DEu), "NVIDIA") &&
          !strcmp(rsx_vk_vendor_name(0x8086u), "Intel") && !strcmp(rsx_vk_vendor_name(0x10005u), "Mesa") &&
          !strcmp(rsx_vk_vendor_name(0x1234u), "unknown vendor"), "vendor names");

    /* VRAM: the largest device-local heap; a host heap, however large, does not count */
    const uint64_t size[3] = { 8176ull << 20, 32768ull << 20, 256ull << 20 };
    const uint32_t flags[3] = { RSX_VK_HEAP_DEVICE_LOCAL, 0, RSX_VK_HEAP_DEVICE_LOCAL };
    CHECK(rsx_vk_device_local_bytes(size, flags, 3) == (8176ull << 20), "VRAM is the 8176 MiB heap");
    const uint32_t none[3] = { 0, 0, 0 };
    CHECK(rsx_vk_device_local_bytes(size, none, 3) == 0, "no device-local heap: 0");
    CHECK(rsx_vk_device_local_bytes(size, flags, 0) == 0, "no heaps: 0");

    printf("test_vk_driver: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
