/*
 * KZ1HD's own exit codes, in one place: tools/release/launch.py (EXIT_CODES) names each one to the player, and
 * tools/tests/test_cpu_check.sh and test_eboot_check.sh check that the two lists agree. Kept away from 1 (a failure without its own code),
 * 2 (usage), 126-165 (a shell's "killed by signal N", 132 being SIGILL) and Windows' NTSTATUS crash codes.
 */
#ifndef KZ_EXIT_H
#define KZ_EXIT_H
#define KZ_EXIT_USAGE 2   /* no EBOOT.elf given */
#define KZ_EXIT_CPU   75  /* cpu_check.c: the CPU is below the x86-64 level this build was compiled for */
#define KZ_EXIT_EBOOT 76  /* main.cpp, eboot_check.h: the EBOOT.elf is not the one this build was lifted from */
#endif
