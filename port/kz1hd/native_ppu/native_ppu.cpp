/* Shared by the native PPU functions (native_ppu.h). */
#include "native_ppu.h"
#include <stdio.h>
#include <stdlib.h>

int kz_native_verify = getenv("KZ_NATIVE_VERIFY") ? 1 : 0;

namespace {
struct Tally { const char* name; unsigned long calls, bad, fallback, skipped; };
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
    fprintf(stderr, "[kz-native] %s: %lu checked, %lu mismatched, %lu ran the lifted body", t.name, t.calls, t.bad, t.fallback);
    if (t.skipped) fprintf(stderr, ", %lu not compared (inputs changed by another thread during the check)", t.skipped);
    fputc('\n', stderr);
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

namespace { void (*s_at_quit[8])(void); }
void kz_native_at_quit(void (*fn)(void))
{
    for (auto& f : s_at_quit) if (!f) { f = fn; return; }
}

/* At quit (main.cpp's kz_quit): every function's final counts, when any was
 * counted, and what native functions asked to print then (kz_native_at_quit). */
void kz_native_report_all(void)
{
    for (const Tally& t : s_tally) if (t.name) report(t);
    for (auto f : s_at_quit) if (f) f();
}

void kz_native_skip(const char* name)
{
    tally(name).skipped++;
}

void kz_native_fallback(const char* name)
{
    Tally& t = tally(name);
    if ((++t.fallback & ((1ul << 16) - 1)) == 0) report(t);
}
