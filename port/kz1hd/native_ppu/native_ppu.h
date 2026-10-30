/*
 * Native replacements for hot lifted PPU functions (the .cpp files here).
 *
 * A guest function that the profile puts near the top is rewritten here in
 * plain C++ from its lifted body: the same loads and stores of guest memory,
 * the same arithmetic in the same order (rounded to single wherever the guest
 * rounds), but without the register file in memory, the condition-register
 * bookkeeping and the guest stack frame. lift.sh passes the names defined
 * here to the PPU lifter (PPU_LIFT_NATIVE), which then emits the lifted body
 * as <name>_lifted and leaves <name> to this directory.
 *
 * A native function keeps the guest ABI: it may change only the volatile
 * registers, and it sets the ones the guest function returns in.
 *
 * KZ_NATIVE_VERIFY=1 runs the lifted body as well and reports any difference
 * in what it wrote; the lifted result is the one kept.
 */
#ifndef KZ_NATIVE_PPU_H
#define KZ_NATIVE_PPU_H
#include "ppu_recomp.h"
#include <math.h>
#include <string.h>

extern int kz_native_verify;
/* Compares `mine` with what the lifted body produced and reports a difference. */
void kz_native_check(const char* name, const void* mine, const void* lifted, size_t n);
/* Under KZ_NATIVE_VERIFY: counts a call the native code left to the lifted body. */
void kz_native_fallback(const char* name);

/* Round to single, as the guest's single-precision instructions do. */
static inline double S(double x) { return (double)(float)x; }
static inline double ldf(uint32_t ea)
{
    uint32_t u = vm_read32(ea);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static inline void stf(uint32_t ea, double v)
{
    float f = (float)v;
    uint32_t u;
    memcpy(&u, &f, 4);
    vm_write32(ea, u);
}
#endif
