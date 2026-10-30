/* Shared by the native PPU functions (native_ppu.h). */
#include "native_ppu.h"
#include <stdio.h>
#include <stdlib.h>

int kz_native_verify = getenv("KZ_NATIVE_VERIFY") ? 1 : 0;

namespace {
struct Tally { const char* name; unsigned long calls, bad, fallback; };
Tally s_tally[32];
Tally& tally(const char* name)
{
    for (Tally& t : s_tally) {
        if (t.name == name) return t;
        if (!t.name) { t.name = name; return t; }
    }
    return s_tally[31];
}
void report(const Tally& t)
{
    fprintf(stderr, "[kz-native] %s: %lu checked, %lu mismatched, %lu ran the lifted body\n",
            t.name, t.calls, t.bad, t.fallback);
}
}  // namespace

void kz_native_check(const char* name, const void* mine, const void* lifted, size_t n)
{
    Tally& t = tally(name);
    t.calls++;
    if (memcmp(mine, lifted, n)) {
        if (t.bad++ < 20) {
            size_t i = 0;
            while (((const uint8_t*)mine)[i] == ((const uint8_t*)lifted)[i]) i++;
            fprintf(stderr, "[kz-native] %s MISMATCH (call %lu) at byte %zu\n", name, t.calls, i);
        }
    }
    if ((t.calls & ((1ul << 20) - 1)) == 0 || (t.calls >= 256 && !(t.calls & (t.calls - 1)))) report(t);
}

void kz_native_fallback(const char* name)
{
    Tally& t = tally(name);
    if ((++t.fallback & ((1ul << 16) - 1)) == 0) report(t);
}
