/* Host test for port/kz1hd/cpu_level.h: kz_cpu_level_of() on the CPUID feature bits of real CPUs (the feature lists
 * of QEMU's CPU models of the same names: Nehalem, SandyBridge, IvyBridge, Opteron_G5, Haswell, Broadwell,
 * Skylake-Client, EPYC, Skylake-Server, Icelake-Server), with the OS state an OS with AVX saves; Haswell under an OS
 * that saves no YMM state is v2; and this machine's own level, printed for the shell test to compare with
 * /proc/cpuinfo (tools/tests/test_cpu_check.sh). Also built for Windows and run under Wine (tools/wine-host-tests.sh).
 * No game data. */
#include <stdio.h>
#include <string.h>
#include "cpu_level.h"

/* leaf 1 ECX */
#define SSE3 (1u << 0)
#define PCLMUL (1u << 1)
#define SSSE3 (1u << 9)
#define FMA (1u << 12)
#define CX16 (1u << 13)
#define SSE41 (1u << 19)
#define SSE42 (1u << 20)
#define MOVBE (1u << 22)
#define POPCNT (1u << 23)
#define AES (1u << 25)
#define XSAVE (1u << 26)
#define OSXSAVE (1u << 27)
#define AVX (1u << 28)
#define F16C (1u << 29)
#define RDRAND (1u << 30)
/* leaf 1 EDX: FPU, CX8, CMOV, MMX, FXSR, SSE, SSE2 */
#define BASE_EDX ((1u << 0) | (1u << 8) | (1u << 15) | (1u << 23) | (1u << 24) | (1u << 25) | (1u << 26))
/* leaf 7 EBX */
#define FSGS (1u << 0)
#define BMI1 (1u << 3)
#define AVX2 (1u << 5)
#define SMEP (1u << 7)
#define BMI2 (1u << 8)
#define ERMS (1u << 9)
#define A512F (1u << 16)
#define A512DQ (1u << 17)
#define RDSEED (1u << 18)
#define ADX (1u << 19)
#define A512CD (1u << 28)
#define A512BW (1u << 30)
#define A512VL (1u << 31)
/* leaf 0x80000001 ECX */
#define LAHF (1u << 0)
#define ABM (1u << 5)
#define SSE4A (1u << 6)
#define FMA4 (1u << 16)
#define TBM (1u << 21)
/* XCR0: x87, SSE, AVX (YMM); and opmask, ZMM_Hi256, Hi16_ZMM */
#define XCR_AVX 0x7u
#define XCR_512 0xE7u

#define V2_ECX (SSE3 | SSSE3 | CX16 | SSE41 | SSE42 | POPCNT)
#define HSW_ECX (V2_ECX | PCLMUL | FMA | MOVBE | AES | XSAVE | OSXSAVE | AVX | F16C | RDRAND)
#define HSW_EBX (FSGS | BMI1 | AVX2 | SMEP | BMI2 | ERMS)

static const struct { const char* name; kz_cpu_bits b; int want; } CPUS[] = {
    {"Nehalem (2008)", {V2_ECX, BASE_EDX, 0, LAHF, 0}, 2},
    {"SandyBridge (2011): AVX, no AVX2", {V2_ECX | PCLMUL | AES | XSAVE | OSXSAVE | AVX, BASE_EDX, 0, LAHF, XCR_AVX}, 2},
    {"IvyBridge (2012): F16C, no AVX2", {V2_ECX | PCLMUL | AES | XSAVE | OSXSAVE | AVX | F16C | RDRAND, BASE_EDX,
                                         FSGS | SMEP | ERMS, LAHF, XCR_AVX}, 2},
    {"Opteron_G5 (Piledriver): FMA, BMI1, no AVX2/BMI2/MOVBE",
     {V2_ECX | PCLMUL | FMA | AES | XSAVE | OSXSAVE | AVX | F16C, BASE_EDX, BMI1, LAHF | ABM | SSE4A | FMA4 | TBM,
      XCR_AVX}, 2},
    {"Haswell (2013)", {HSW_ECX, BASE_EDX, HSW_EBX, LAHF | ABM, XCR_AVX}, 3},
    {"Broadwell (2014)", {HSW_ECX, BASE_EDX, HSW_EBX | RDSEED | ADX, LAHF | ABM, XCR_AVX}, 3},
    {"Skylake-Client (2015)", {HSW_ECX, BASE_EDX, HSW_EBX | RDSEED | ADX, LAHF | ABM, XCR_AVX}, 3},
    {"EPYC (Zen, 2017)", {HSW_ECX, BASE_EDX, HSW_EBX | RDSEED | ADX, LAHF | ABM | SSE4A, XCR_AVX}, 3},
    {"Skylake-Server (2017): AVX-512",
     {HSW_ECX, BASE_EDX, HSW_EBX | RDSEED | ADX | A512F | A512DQ | A512CD | A512BW | A512VL, LAHF | ABM, XCR_512}, 4},
    {"Icelake-Server (2019): AVX-512",
     {HSW_ECX, BASE_EDX, HSW_EBX | RDSEED | ADX | A512F | A512DQ | A512CD | A512BW | A512VL, LAHF | ABM, XCR_512}, 4},
    {"Haswell, OS saves no YMM state (XCR0 = x87|SSE)", {HSW_ECX, BASE_EDX, HSW_EBX, LAHF | ABM, 0x3}, 2},
    {"Haswell without OSXSAVE", {HSW_ECX & ~OSXSAVE, BASE_EDX, HSW_EBX, LAHF | ABM, 0}, 2},
    {"Skylake-Server, OS saves no ZMM state", {HSW_ECX, BASE_EDX, HSW_EBX | A512F | A512DQ | A512CD | A512BW | A512VL,
                                               LAHF | ABM, XCR_AVX}, 3},
    {"Nehalem without LAHF/SAHF in 64-bit mode", {V2_ECX, BASE_EDX, 0, 0, 0}, 1},
    {"Core 2 (no SSE4.2, no POPCNT)", {SSE3 | SSSE3 | CX16 | SSE41, BASE_EDX, 0, LAHF, 0}, 1},
};

int main(void)
{
    int fails = 0;
    for (size_t i = 0; i < sizeof CPUS / sizeof CPUS[0]; i++) {
        int got = kz_cpu_level_of(&CPUS[i].b);
        if (got != CPUS[i].want) fails++;
        printf("%s %s: x86-64-v%d%s\n", got == CPUS[i].want ? "ok  " : "FAIL", CPUS[i].name, got,
               got == CPUS[i].want ? "" : " (expected another level)");
    }
    /* The launcher's rule (#133): below v3 it refuses at once, unless KZ_MARCH chose the build's level */
    int rule = kz_launcher_refuses(2, 0) && !kz_launcher_refuses(3, 0) && !kz_launcher_refuses(4, 0) &&
               !kz_launcher_refuses(2, 1) && kz_launcher_refuses(1, 0);
    char msg[400];
    kz_cpu_message(msg, sizeof msg, 2, KZ_CPU_LEVEL_DEFAULT);
    rule = rule && strstr(msg, "needs a CPU with x86-64-v3 (AVX2") && strstr(msg, "this one has x86-64-v2");
    printf("%s the launcher refuses a CPU below x86-64-v3 with the CPU message, unless KZ_MARCH is set\n",
           rule ? "ok  " : "FAIL");
    if (!rule) fails++;
    int here = kz_cpu_level();
    printf("this CPU: x86-64-v%d\n", here);
    if (here < 1 || here > 4) fails++;
    printf("test_cpu_level: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
