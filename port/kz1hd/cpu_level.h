/*
 * The x86-64 level of this CPU (1 = baseline, 2, 3 = Haswell-class, 4 = AVX-512), from CPUID leaves 1, 7 and
 * 0x80000001 and XCR0, the register state the operating system saves (XGETBV). Header only, plain C, no
 * __builtin_cpu_supports (it needs libgcc's __cpu_model, which the pinned Zig toolchain does not link). Used by
 * cpu_check.c (before the game's own code runs), by the launcher's --cpu-check (installer/launcher/launcher.c, for
 * the setup and the first run on Windows) and by tools/tests/test_cpu_level.c, which feeds kz_cpu_level_of()
 * the CPUID bits of real CPUs. The levels are the x86-64 psABI's microarchitecture levels; the AVX ones also need
 * the OS to save the YMM (and for v4 the ZMM and mask) state, as a CPU with AVX under an OS without it cannot use it.
 * Written here (after Project Helghast's port/spu/kz3_cpu_check.c, undecuple, MIT, for the idea).
 */
#ifndef KZ_CPU_LEVEL_H
#define KZ_CPU_LEVEL_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t l1_ecx, l1_edx;   /* leaf 1 */
    uint32_t l7_ebx;           /* leaf 7, subleaf 0 (0 when the CPU has no leaf 7) */
    uint32_t e1_ecx;           /* leaf 0x80000001 (0 when missing) */
    uint64_t xcr0;             /* XGETBV(0) (0 without OSXSAVE) */
} kz_cpu_bits;

#define KZ_BIT(r, n) (((r) >> (n)) & 1u)

/* The message for a CPU below the build's level, the same words tools/release/launch.py shows for KZ_EXIT_CPU
 * (cpu_check.c, the launcher). */
static inline void kz_cpu_message(char* out, size_t n, int have, int need)
{
    snprintf(out, n,
             "This build of Project Vekta needs a CPU with x86-64-v%d (%s), and this one has x86-64-v%d. "
             "The game cannot run on it.",
             need, need >= 4 ? "AVX-512: Intel Skylake-X, Ice Lake or later, AMD Zen 4 or later"
                             : need == 3 ? "AVX2, FMA, BMI2: Intel Haswell (2013) or AMD Excavator (2015) and later"
                                         : "SSE4.2, POPCNT: Intel Nehalem (2008) or AMD Bulldozer (2011) and later",
             have);
}

/* The level a build made with the default KZ_MARCH (x86-64-v3, tools/kzbuild.py) needs. The Windows launcher
 * refuses a CPU below it before it starts Python, so the player hears it at once: started cold, Python and a
 * first scan of the 100 MB KZ1HD.exe took 30 to 90 s before KZ1HD's own check spoke (#133). A KZ_MARCH set in the
 * environment may have built for another level: the launcher then leaves the check to KZ1HD. */
#define KZ_CPU_LEVEL_DEFAULT 3
static inline int kz_launcher_refuses(int have, int march_set)
{
    return !march_set && have < KZ_CPU_LEVEL_DEFAULT;
}

/* Pure: the level these bits give. */
static inline int kz_cpu_level_of(const kz_cpu_bits* b)
{
    /* v2: CMPXCHG16B, LAHF/SAHF, POPCNT, SSE3, SSE4.1, SSE4.2, SSSE3 */
    int v2 = KZ_BIT(b->l1_ecx, 13) && KZ_BIT(b->e1_ecx, 0) && KZ_BIT(b->l1_ecx, 23) && KZ_BIT(b->l1_ecx, 0) &&
             KZ_BIT(b->l1_ecx, 19) && KZ_BIT(b->l1_ecx, 20) && KZ_BIT(b->l1_ecx, 9);
    if (!v2) return 1;
    /* v3: AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT, MOVBE, OSXSAVE, and the OS saves XMM and YMM (XCR0 bits 1, 2) */
    int v3 = KZ_BIT(b->l1_ecx, 28) && KZ_BIT(b->l7_ebx, 5) && KZ_BIT(b->l7_ebx, 3) && KZ_BIT(b->l7_ebx, 8) &&
             KZ_BIT(b->l1_ecx, 29) && KZ_BIT(b->l1_ecx, 12) && KZ_BIT(b->e1_ecx, 5) && KZ_BIT(b->l1_ecx, 22) &&
             KZ_BIT(b->l1_ecx, 27) && (b->xcr0 & 0x6) == 0x6;
    if (!v3) return 2;
    /* v4: AVX512F, BW, CD, DQ, VL, and the OS saves the opmask and ZMM state (XCR0 bits 5, 6, 7) */
    int v4 = KZ_BIT(b->l7_ebx, 16) && KZ_BIT(b->l7_ebx, 30) && KZ_BIT(b->l7_ebx, 28) && KZ_BIT(b->l7_ebx, 17) &&
             KZ_BIT(b->l7_ebx, 31) && (b->xcr0 & 0xE0) == 0xE0;
    return v4 ? 4 : 3;
}

#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>
/* This CPU's bits. */
static inline kz_cpu_bits kz_cpu_read(void)
{
    kz_cpu_bits b = {0, 0, 0, 0, 0};
    unsigned a, bx, c, d;
    unsigned max = __get_cpuid_max(0, 0);
    if (max >= 1) {
        __cpuid(1, a, bx, c, d);
        b.l1_ecx = c;
        b.l1_edx = d;
    }
    if (max >= 7) {
        __cpuid_count(7, 0, a, bx, c, d);
        b.l7_ebx = bx;
    }
    if (__get_cpuid_max(0x80000000u, 0) >= 0x80000001u) {
        __cpuid(0x80000001u, a, bx, c, d);
        b.e1_ecx = c;
    }
    if (KZ_BIT(b.l1_ecx, 27)) {   /* OSXSAVE: XGETBV is there (the instruction by its bytes: no -mxsave needed) */
        unsigned lo, hi;
        __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0));
        b.xcr0 = ((uint64_t)hi << 32) | lo;
    }
    return b;
}

static inline int kz_cpu_level(void)
{
    kz_cpu_bits b = kz_cpu_read();
    return kz_cpu_level_of(&b);
}
#endif
#endif
