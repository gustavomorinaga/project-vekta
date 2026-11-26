/*
 * Project Vekta's exit codes, the one table: KZ1HD's own (USAGE, CPU, EBOOT) and the launcher's (tools/release/
 * launch.py: Project Vekta.exe, project-vekta, --prepare), each with the one line the player is told. launch.py reads
 * this file (exit_codes()), so the line format is fixed: #define KZ_EXIT_<NAME> <code> followed by a one-line comment
 * holding the fix text. recomplauncher.json, docs/TROUBLESHOOTING.md and the tests take the codes from here.
 * Kept away from 126-165 (a shell's "killed by signal N", 132 being SIGILL) and Windows' NTSTATUS crash codes;
 * 130 is the shell's Ctrl+C, which firstrun.py has always used for a stopped run.
 */
#ifndef KZ_EXIT_H
#define KZ_EXIT_H
#define KZ_EXIT_FAILED        1   /* Something else went wrong: the end of work/play.log or the newest work/logs/firstrun-*.log says what. */
#define KZ_EXIT_USAGE         2   /* Wrong arguments: run with --help to see them. */
#define KZ_EXIT_NOT_PREPARED  70  /* The game is not built yet: start Project Vekta, or run its prepare step (--prepare), to check your files and build it. */
#define KZ_EXIT_WRONG_EDITION 71  /* These files are another edition or version of the game: the port needs NPUA80856, NPEA00418 or BCES01743 version 01.00 (docs/GETTING-FILES.md). */
#define KZ_EXIT_FILE_MISSING  72  /* A file the port needs is missing or still encrypted: give the game folder (--game), or let your RPCS3 make the decrypted copy (--rpcs3). */
#define KZ_EXIT_NO_RPCS3      73  /* No RPCS3 with the game installed was found in its usual places: give its folder (--rpcs3 DIR), or your files (--game, --eboot). */
#define KZ_EXIT_BUILD_FAILED  74  /* The build stopped: the reason is at the end of the newest work/logs/firstrun-*.log; after a fix, prepare again (it resumes). */
#define KZ_EXIT_CPU           75  /* This CPU is too old: the build needs x86-64-v3 (AVX2, FMA, BMI2: Intel Haswell or AMD Excavator, 2013 and later). */
#define KZ_EXIT_EBOOT         76  /* This build was made from another EBOOT.elf than the game folder's: run Verify and rebuild (Windows) or ./project-vekta --prepare (Linux). */
#define KZ_EXIT_NO_VULKAN     77  /* No Vulkan driver was found: install or update your graphics card's driver. */
#define KZ_EXIT_GPU_LOST      78  /* The graphics driver stopped responding (the GPU was lost): update the graphics driver, and attach the support package to a bug report. */
#define KZ_EXIT_STOPPED       130 /* Stopped by the player or the time limit: start again, and the build resumes where it stopped. */
#endif
