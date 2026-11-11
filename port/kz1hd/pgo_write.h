/*
 * Killzone 1 HD -- writing the profile of an instrumented build (KZ_PGO_GEN, tools/pgo.sh).
 *
 * clang's profile runtime writes the counters from an atexit handler, but the game always leaves through
 * _exit (main.cpp kz_quit: a clean exit would run the guest's destructors), so a run that quits on its own
 * (closing the window, KZ_ROUTE_EXIT at the end of a replay) wrote nothing and only the KZ_PGO_DUMP timer
 * did. kz_pgo_write() writes the counters now: kz_quit calls it before _exit, the timer after its delay.
 * A later write replaces the earlier one (LLVM_PROFILE_FILE names the file), so the last one has every count.
 *
 * __llvm_profile_write_file is weak: in a build without -fprofile-instr-generate it is null and nothing
 * happens. Returns 1 when a profile was written.
 */
#ifndef KZ_PGO_WRITE_H
#define KZ_PGO_WRITE_H

#ifdef __cplusplus
extern "C" {
#endif

int __llvm_profile_write_file(void) __attribute__((weak));

static inline int kz_pgo_write(void)
{
    if (!__llvm_profile_write_file) return 0;
    return __llvm_profile_write_file() == 0;
}

#ifdef __cplusplus
}
#endif

#endif
