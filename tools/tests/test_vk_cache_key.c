/* The keys of the Vulkan backend's caches on disk (patch 2056): the same program gets another SPIR-V file, and the
 * pipeline list another header, when the translator's id differs, and the same ones when it does not. Before the
 * patch the key was the HLSL, the stage and VK_CACHE_VERSION only, so a build with another translator read an older
 * build's SPIR-V as its own (Project Pyrrhus's issue #25: an update over an older install lost the sight's reticle). */
#include <stdio.h>
#include "rsx_vk_cache_key.h"

static int fail(const char* why) { fprintf(stderr, "FAIL: %s\n", why); return 1; }

int main(void)
{
    const char* ps = "float4 main() : SV_Target { return float4(1, 0, 1, 1); }";
    const uint64_t a = rsx_vk_spv_key(ps, 1, 1, 2, 0x1111222233334444ull);
    if (a != rsx_vk_spv_key(ps, 1, 1, 2, 0x1111222233334444ull)) return fail("the same inputs give another key");
    if (a == rsx_vk_spv_key(ps, 1, 1, 2, 0x1111222233334445ull)) return fail("another translator, the same key");
    if (a == rsx_vk_spv_key(ps, 1, 1, 2, 0x9111222233334444ull)) return fail("the id's top byte is not in the key");
    if (a == rsx_vk_spv_key(ps, 0, 1, 2, 0x1111222233334444ull)) return fail("another stage, the same key");
    if (a == rsx_vk_spv_key(ps, 1, 0, 2, 0x1111222233334444ull)) return fail("invariant off, the same key");
    if (a == rsx_vk_spv_key(ps, 1, 1, 3, 0x1111222233334444ull)) return fail("another VK_CACHE_VERSION, the same key");
    if (a == rsx_vk_spv_key("float4 main() : SV_Target { return 0; }", 1, 1, 2, 0x1111222233334444ull))
        return fail("another program, the same key");
    if (rsx_vk_plist_translator_word(0x1111222233334444ull) == rsx_vk_plist_translator_word(0x1111222233334445ull))
        return fail("another translator, the same pipeline list header");
    if (rsx_vk_plist_translator_word(0x1111222233334444ull) == 0 || rsx_vk_plist_translator_word(0) == 0)
        return fail("a pipeline list header word of 0, the old lists' value");
    puts("test_vk_cache_key: ok");
    return 0;
}
