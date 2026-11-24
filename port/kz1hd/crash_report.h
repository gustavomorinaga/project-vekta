/*
 * Crash reports, the hang watchdog and KZ_CRASH_TEST (crash_report.c). The log formats are in
 * docs/dev/crash-reports.md; tools/release/support.py and the launcher read them.
 *
 *   [kz-crash] begin ... [kz-crash] end   an unhandled fault (or abort()) on any thread: the exception or signal, the
 *                                         faulting address and module+offset, the thread, the uptime, the registers,
 *                                         up to 32 frames as module+offset and a signature KZ-XXXX-XXXX; a copy goes
 *                                         to $KZ_CRASH_DIR/crash-YYYYmmdd-HHMMSS.txt
 *   [kz-hang] begin ... [kz-hang] end     no new frame for 5 s (and again at 30 s) while the game had been showing
 *                                         frames and the window is not minimized: every thread's stack; a copy goes
 *                                         to $KZ_CRASH_DIR/hang-YYYYmmdd-HHMMSS.txt. The game is never stopped.
 *
 * Frames are module+offset only (Windows: the offset from the module's base, an RVA; Linux: the address less the
 * module's load bias, the ELF virtual address addr2line and nm use). Nothing is symbolized in the game: the lifted
 * functions are named by guest address in the symbol table, so the player's support package can do it later.
 */
#ifndef KZ_CRASH_REPORT_H
#define KZ_CRASH_REPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* main(), right after the version line and before ppu_run (whose ps3_stdio_init installs its own filter over this
 * one and calls it): the unhandled-exception filter (Linux: the same filter, through win32_compat's signal
 * dispatcher), the SIGABRT handler, an alternate signal stack for this thread (Linux) or a stack guarantee
 * (Windows), so a stack overflow on the main thread is still reported. version_line goes into each report. */
void kz_crash_install(const char* version_line);

/* The hang watchdog (KZ_HANG_WATCH, default 5: the first report after that many seconds without a new frame, the
 * second at six times as long; 0 = off). frames() is the frame counter (cellGcm_flip_request_count); the watch arms
 * once it has moved past 0, so a long boot or the data step never counts as a hang. */
void kz_hang_watch_start(unsigned (*frames)(void));

/* The frame loop, every round: it ran, and whether the window is minimized now (a minimized window does not count
 * as hung). */
void kz_hang_tick(int minimized);

/* KZ_CRASH_TEST=<kind> (default unset), for testing a build: null-main, null-thread, abort, illegal, stack, fastfail,
 * hang; KZ_CRASH_TEST_DELAY=<seconds> (default 5) before it fires. kz_crash_test_start() (main(), after
 * kz_crash_install) starts the thread kinds (null-thread, hang); kz_crash_test_main_point() (main(), right before
 * ppu_run) runs the main-thread kinds there, after the delay and after ps3_stdio_init, so the report goes through the
 * same filter chain as a crash in the game; kz_crash_test_hang_point() (the frame loop) stops that loop for good
 * once the hang test fires. */
void kz_crash_test_start(void);
void kz_crash_test_main_point(void);
void kz_crash_test_hang_point(void);

/* The functions the null tests fault in, named so a test can check that frame #0 lands in them. */
int  kz_crash_null_read(volatile int* p);
void kz_crash_null_write(volatile int* p);

#ifdef __cplusplus
}
#endif
#endif
