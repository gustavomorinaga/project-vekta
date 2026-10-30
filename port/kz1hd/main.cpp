/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include <signal.h>
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"
#include "../ps3_trace.h"   /* runtime/, via the runtime/ppu include directory */

#include <chrono>
#include <string>
#include <thread>
#include <vector>
extern "C" uint32_t g_rwatch_dyn_lo, g_rwatch_dyn_len, g_wwatch_dyn_lo, g_wwatch_dyn_len, g_ld_span, g_st_span, g_barrier_sync_watch, g_wval_dyn, g_wval_dyn_n;
extern "C" int vm_inline_ok;
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <malloc.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

/* The heap in use, for the stats line: glibc's allocator count, or the
 * process's private bytes on Windows (no mallinfo2 there). */
static size_t kz_heap_bytes(void)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    return GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc) ? pmc.PrivateUsage : 0;
#else
    return mallinfo2().uordblks;
#endif
}

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" void cellGame_init_from_paramsfo(const char* sfo_path);   /* cellGame.c */
extern "C" void kz_pc_settings_env(void);   /* pcoptions.cpp */
extern "C" void kz_view_update(void);       /* pcoptions.cpp */
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);
unsigned cellGcm_flip_written_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(PS3RECOMP_RSX_VULKAN)
/* The project targets Vulkan on every platform (docs/dev/recomp-plan.md). */
extern "C" int  rsx_vulkan_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_vulkan_backend_present(void);
extern "C" int  rsx_vulkan_backend_pump_messages(void);
#  define rsx_backend_init    rsx_vulkan_backend_init
#  define rsx_backend_present rsx_vulkan_backend_present
#  define rsx_backend_pump    rsx_vulkan_backend_pump_messages
#  define RSX_BACKEND_NAME    "Vulkan"
#elif defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

/* KZ_STATS: presents in the last window, and the longest gap between two. */
static unsigned s_present_n, s_present_gap_max_us;

static void present_guest_frame(void)
{
    if (g_ps3_trace_on) { const uint64_t t = ps3_trace_now(); ps3_trace_span("flip", t, t + 1000, 0); }
    { static uint64_t last;
      const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      if (last && now - last > s_present_gap_max_us) s_present_gap_max_us = (unsigned)(now - last);
      last = now; s_present_n++; }
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

extern "C" void kz_prof_arm(void);

/* KZ_TICK_HZ=<n>: the game clock's tick rate (the game's is 30). The main
 * loop (func_0021C0F8) turns ms into ticks with the double at 0x21C0F0 and
 * waits for at least one whole tick, so the frame rate cannot pass the tick
 * rate; func_00219320 sets the seconds per tick at +0x54 of the clock object
 * (pointer at 0x828734). Both change together, so a tick stays the same span
 * of game time in seconds. Called from the frame clock: the clock object only
 * exists once the game has made it, and a later re-init is caught too. */
/* The rates in force, changeable while running (the PC options menu):
 * g_kz_tick_hz the game clock (30 = the game's), g_kz_guest_vblank_hz the
 * vblanks the game sees (0 = KZ_VBLANK_HZ's). -1 = from the environment. */
extern "C" { volatile int g_kz_tick_hz = -1; volatile double g_kz_guest_vblank_hz = -1;
             volatile double g_kz_flip_hz = -1;   /* the flip cadence, KZ_VBLANK_HZ */ }

static void kz_tick_rate(void)
{
    if (g_kz_tick_hz < 0) { const char* e = getenv("KZ_TICK_HZ"); const int v = e ? atoi(e) : 30; g_kz_tick_hz = v > 0 ? v : 30; }
    if (!vm_base) return;
    const int hz = g_kz_tick_hz;
    static int applied = 30;         /* the rate the clock object holds */
    static uint32_t cap30;           /* the game's own tick cap */
    static double ms30 = -1;         /* the game's own ticks per ms */
    if (ms30 < 0) { uint64_t u; memcpy(&u, vm_base + 0x21C0F0u, 8); u = __builtin_bswap64(u); memcpy(&ms30, &u, 8); }
    uint32_t p; memcpy(&p, vm_base + 0x828734u, 4); p = __builtin_bswap32(p);
    if (!p || p >= 0xF0000000u) return;
    uint32_t v; memcpy(&v, vm_base + p + 0x54, 4); v = __builtin_bswap32(v);
    const float cur = 1.0f / (float)applied; uint32_t cur_bits; memcpy(&cur_bits, &cur, 4);
    if (v == 0x3D088889u) {          /* 1/30: the game's (also after a re-init of the clock) */
        /* the game's own cap, read once: a re-init of the clock resets +0x54
         * but leaves +0x74 as we scaled it */
        if (!cap30) { uint32_t c; memcpy(&c, vm_base + p + 0x74, 4); cap30 = __builtin_bswap32(c); }
        applied = 30;
        if (hz == 30) return;
    } else if (applied == 30 || v != cur_bits || hz == applied) return;   /* not ours, or in force */
    { const double d = hz == 30 ? ms30 : hz / 1000.0; uint64_t u; memcpy(&u, &d, 8); u = __builtin_bswap64(u);
      memcpy(vm_base + 0x21C0F0u, &u, 8); }
    const float f = 1.0f / (float)hz; memcpy(&v, &f, 4); v = __builtin_bswap32(v);
    memcpy(vm_base + p + 0x54, &v, 4);
    /* +0x74 caps the ticks one frame may advance: the same span of game time
     * is hz/30 times as many ticks. */
    const uint32_t cap2 = cap30 * (uint32_t)hz / 30u;
    if (cap2) { uint32_t c = __builtin_bswap32(cap2); memcpy(vm_base + p + 0x74, &c, 4); }
    static int said;
    if (said++ < 8) fprintf(stderr, "[kz] game clock at %d Hz (clock object 0x%08X, tick cap %u -> %u)\n",
                            hz, p, cap30, cap2);
    applied = hz;
}

/* Quitting: the window closed, or Quit Game in the PC screen. The game
 * itself never exits (a console title), so the process ends here: the save
 * operations in progress finish first (a half-written save is one the game
 * may refuse), the logs are flushed, and nothing else is torn down. */
extern "C" int cellSaveData_quiesce(unsigned max_ms);
extern "C" void kz_quit(const char* why)
{
    static volatile int once;
    if (__atomic_exchange_n(&once, 1, __ATOMIC_ACQ_REL)) return;
    fprintf(stderr, "[kz] quitting (%s)\n", why);
    if (!cellSaveData_quiesce(5000)) fprintf(stderr, "[kz] a save was still being written after 5 s\n");
    fflush(NULL);
    _exit(0);
}

/* SIGTERM, SIGINT (Ctrl+C in the terminal) and SIGHUP quit as closing the
 * window does; the frame clock does the quitting, outside the handler. */
static volatile sig_atomic_t s_quit_signal;
static void on_quit_signal(int) { s_quit_signal = 1; }

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    /* KZ_VBLANK_HZ: the emulated vblank rate (default 59.94, the PS3's: two
     * vblanks are then one tick of the game's 30 Hz clock, so a frame that fits
     * is shown every 33.4 ms exactly).
     * The guest flips on a vblank, so its frame rate is this divided by a
     * whole number of ticks per frame. */
    if (g_kz_flip_hz < 0) { const char* hz_env = getenv("KZ_VBLANK_HZ");
                            g_kz_flip_hz = hz_env && atof(hz_env) > 0 ? atof(hz_env) : 59.94; }
    uint64_t tick_us = (uint64_t)(1e6 / g_kz_flip_hz);
    double tick_hz = g_kz_flip_hz;
    auto now_us = [] { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count(); };
    uint64_t next_tick = now_us();

    /* KZ_RSX_POLL_US: how often the FIFO drains between vblank ticks
     * (default 100). Every fence the game waits on -- a label a job or the
     * PPU spins on -- costs up to one period. The real RSX writes them in microseconds. */
    const char* poll_env = getenv("KZ_RSX_POLL_US");
    const unsigned poll_us = poll_env ? (unsigned)atoi(poll_env) : 100u;
    for (;;) {
#ifdef _WIN32
        Sleep(poll_us / 1000u);
#else
        usleep(poll_us);
#endif
        uint64_t now_t = now_us();
        ULONGLONG now = now_t / 1000;   /* ms, for the stats line */
        kz_tick_rate();
        if (g_kz_flip_hz != tick_hz && g_kz_flip_hz > 0) { tick_hz = g_kz_flip_hz; tick_us = (uint64_t)(1e6 / tick_hz); next_tick = now_t; }

        int fired = 0;
        /* One guest flip per refresh: the drain stops at each flip word, and
         * draining again before the next tick would let the title run as fast
         * as the host can decode (thousands of flips a second on KZ1 HD's
         * title screen, where the real RSX holds the flip until vblank). */
        static int flipped_this_tick = 0;
        if ((long long)(now_t - next_tick) >= 0) flipped_this_tick = 0;
        /* KZ_GUEST_VBLANK_HZ: the vblank interrupts the game itself sees, apart
         * from the rate flips are shown at (KZ_VBLANK_HZ). Part of the game
         * logic counts vblanks, so a 144 Hz display must not hand it 144 a
         * second; default: the same as KZ_VBLANK_HZ. */
        { static uint64_t gv_us = 1, next_gv; static double gv_hz = -2;
          if (g_kz_guest_vblank_hz < 0) { const char* e = getenv("KZ_GUEST_VBLANK_HZ"); g_kz_guest_vblank_hz = e && atof(e) > 0 ? atof(e) : 0; }
          if (gv_hz != g_kz_guest_vblank_hz) { gv_hz = g_kz_guest_vblank_hz;
                            gv_us = gv_hz > 0 ? (uint64_t)(1e6 / gv_hz) : 0; next_gv = now_t; }
          if (gv_us) { int n = 0;
                       while ((long long)(now_t - next_gv) >= 0 && n++ < 8) { cellGcmTickVBlank(); next_gv += gv_us; }
                       if (n >= 8) next_gv = now_t + gv_us; } }
        const int s_own_vblank = g_kz_guest_vblank_hz > 0;
        while ((long long)(now_t - next_tick) >= 0 && fired < 240) {
            if (!s_own_vblank) cellGcmTickVBlank();
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok && !flipped_this_tick) {
                unsigned before = cellGcm_flip_request_count();
                cellGcm_rsx_process_fifo();
                flipped_this_tick = cellGcm_flip_request_count() != before;
            }
            next_tick += tick_us;
            fired++;
        }
        if (fired >= 240) next_tick = now_t;   /* fell too far behind -- resync */

        /* KZ_STATS=1: one line every 2 s with the flips the guest asked for. */
        static int stats = -1;
        if (stats < 0) stats = getenv("KZ_STATS") != nullptr;
        static ULONGLONG next_stats = 0;
        /* PS3_TRACE="0,..." and KZ_PROF="0,...": start recording once gameplay is on screen, which
         * here means 10 s in a row at 17-32 guest flips a second, Level 1 now (loading
         * screens and menus run at the vblank rate). */
        { static ULONGLONG next_check; static unsigned last_flips; static int steady;
          if (now >= next_check) {
              const unsigned f = cellGcm_flip_request_count(), d = f - last_flips;
              last_flips = f; next_check = now + 2000;
              steady = (d >= 34 && d <= 64) ? steady + 1 : 0;
              if (steady == 5) { ps3_trace_arm(); kz_prof_arm(); }
          } }
        /* KZ_MEMSNAP=<dir> (reverse engineering): when <dir>/req appears, guest RAM
         * (0x0-0x10000000 and 0x40000000-0x50000000) goes to <dir>/snap<n>.bin. */
        { static const char* snap = getenv("KZ_MEMSNAP"); static int n;
          if (snap) {
              std::string req = std::string(snap) + "/req";
              if (FILE* r = fopen(req.c_str(), "rb")) {
                  fclose(r); remove(req.c_str());
                  std::string out = std::string(snap) + "/snap" + std::to_string(n++) + ".bin";
                  if (FILE* f = fopen(out.c_str(), "wb")) {
                      fwrite(vm_base, 1, 0x10000000, f); fwrite(vm_base + 0x40000000u, 1, 0x10000000, f); fclose(f);
                  }
                  fprintf(stderr, "[kz] memsnap %s\n", out.c_str());
              }
          } }
        kz_view_update();   /* pcoptions.cpp: the cameras follow the window's aspect */
        /* KZ_POKE=<file> (reverse engineering), re-read every second:
         *   <hex addr> <hex u32>   written into guest RAM every frame
         *   rw|ww <hex addr> <hex len>   move the runtime's dynamic read/write watch
         *   wv <hex value> <hex n>   log the next n stores of that value, with guest stacks
         *   capture   the next frames' passes, as F12 (rsx_draw_engine.c)
         *   cam   find the camera blocks (near, far, fov, aspect 16:9) and log them */
        { static const char* pk = getenv("KZ_POKE"); static ULONGLONG next_pk;
          static std::vector<std::pair<uint32_t, uint32_t>> pokes;
          if (pk) {
              if (now >= next_pk) {
                  next_pk = now + 1000; pokes.clear();
                  if (FILE* f = fopen(pk, "rb")) { char w[16]; unsigned a, v;
                      while (fscanf(f, "%15s", w) == 1) {
                          if (!strcmp(w, "capture")) {   /* as F12: the next frames' passes */
                              extern volatile int g_rsx_capture_request;
                              g_rsx_capture_request = 1;
                          } else if (!strcmp(w, "cam")) {
                              for (uint32_t x = 0x40000000u; x < 0x50000000u; x += 4) {
                                  uint32_t t; memcpy(&t, vm_base + x, 4);
                                  if (t != 0x3A8EE33Fu) continue;   /* 0x3FE38E3A byte-swapped */
                                  float nf[3]; for (int k = 0; k < 3; k++) { uint32_t u; memcpy(&u, vm_base + x - 12 + 4 * k, 4); u = __builtin_bswap32(u); memcpy(&nf[k], &u, 4); }
                                  fprintf(stderr, "[kz-cam] aspect at 0x%08X near %g far %g fov %g\n", x, nf[0], nf[1], nf[2]);
                              }
                          } else if (!strcmp(w, "wv") && fscanf(f, "%x %x", &a, &v) == 2) {
                              /* the next <v> stores of the value <a>, with their guest stacks */
                              g_barrier_sync_watch = 0x100; g_st_span = 0; g_wval_dyn = a; g_wval_dyn_n = v;
                          } else if ((!strcmp(w, "rw") || !strcmp(w, "ww")) && fscanf(f, "%x %x", &a, &v) == 2) {
                              /* out-of-line loads (and stores, through the sync-watch arm) from now on */
                              if (w[0] == 'r') { vm_inline_ok = 0; g_ld_span = 0; g_rwatch_dyn_lo = a; g_rwatch_dyn_len = v; }
                              else { g_barrier_sync_watch = 0x100; g_st_span = 0; g_wwatch_dyn_lo = a; g_wwatch_dyn_len = v; }
                          } else if (sscanf(w, "%x", &a) == 1 && fscanf(f, "%x", &v) == 1) pokes.push_back({a, v});
                      }
                      fclose(f); }
              }
              for (auto& [a, v] : pokes) { const uint32_t b = __builtin_bswap32(v); memcpy(vm_base + a, &b, 4); }
          } }
        if (stats && now >= next_stats) {
            /* ticks: the game clock's tick counter (clock object +0x6C), to see game time against real time */
            uint32_t ticks = 0;
            { uint32_t p; memcpy(&p, vm_base + 0x828734u, 4); p = __builtin_bswap32(p);
              if (p && p < 0xF0000000u) { memcpy(&ticks, vm_base + p + 0x6C, 4); ticks = __builtin_bswap32(ticks); } }
            fprintf(stderr, "[kz-stats] t=%llus flips=%u queued=%d gap=%ums heap=%zuMB ticks=%u\n", (unsigned long long)(now / 1000),
                    cellGcm_flip_request_count(),
                    (int)(cellGcm_flip_written_count() - cellGcm_flip_request_count()),
                    s_present_gap_max_us / 1000, kz_heap_bytes() >> 20, ticks);
            s_present_gap_max_us = 0; s_present_n = 0;
            next_stats = now + 2000;
        }

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            /* A flip is presented on the next vblank tick, as the RSX shows it:
             * presenting it the moment the drain reached it made the frame
             * interval wander between 27 and 41 ms at a steady 30 fps. */
            if (!flipped_this_tick) {
                unsigned before = cellGcm_flip_request_count();
                cellGcm_rsx_process_fifo();
                flipped_this_tick = cellGcm_flip_request_count() != before;
            }

            if (rsx_backend_pump() != 0) kz_quit("window closed");
            if (s_quit_signal) kz_quit("signal");
            /* Before the first flip present freely, so the window is not
             * blank during boot. */
            if (cellGcm_flip_request_count() == 0) rsx_backend_present();
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. mmap rather than calloc: glibc's
     * calloc returns the mapping plus a 16-byte header, and vm_commit's
     * mprotect then fails on every guest page (sys_memory_allocate -> ENOMEM). */
    void* m = mmap(NULL, 0xE0000000u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    vm_base = m == MAP_FAILED ? nullptr : (uint8_t*)m;
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

extern "C" void kz_prof_start(void);

/* KZ_PGO_DUMP=<seconds>: an instrumented build (KZ_PGO_GEN) writes its profile
 * then (LLVM_PROFILE_FILE names it); the run is killed, never exits cleanly. */
extern "C" int __llvm_profile_write_file(void) __attribute__((weak));
static void kz_pgo_dump_start(void)
{
    const char* e = getenv("KZ_PGO_DUMP");
    if (!e || !__llvm_profile_write_file) return;
    const int secs = atoi(e);
    std::thread([secs] {
        std::this_thread::sleep_for(std::chrono::seconds(secs));
        __llvm_profile_write_file();
        fprintf(stderr, "[kz-pgo] profile written\n");
    }).detach();
}

#ifdef _WIN32
/* Where the log goes on Windows. Started from a shell (or Wine from one) it
 * stays on stderr; started without one (Explorer, Steam, Proton, which drops
 * it) it goes to kz1hd.log beside the exe, rewritten every run. KZ_CONSOLE=1
 * opens a console window for it instead, KZ_LOG=<file> names the file. */
static void kz_log_setup(void)
{
    const char* path = getenv("KZ_LOG");
    if (getenv("KZ_CONSOLE") && AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        return;
    }
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (!path && h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) return;
    char file[MAX_PATH];
    if (!path) {
        DWORD n = GetModuleFileNameA(NULL, file, MAX_PATH);
        char* slash = strrchr(file, '\\');
        if (!n || n >= MAX_PATH || !slash || (size_t)(slash - file) + sizeof "\\kz1hd.log" > MAX_PATH) return;
        strcpy(slash, "\\kz1hd.log");
        path = file;
    }
    /* Both streams appending to one file: a GUI process starts without valid
     * stdout and stderr streams, which a dup2 of the descriptors leaves so. */
    FILE* f = fopen(path, "w");
    if (!f) return;
    fclose(f);
    if (freopen(path, "a", stdout)) setvbuf(stdout, NULL, _IONBF, 0);
    if (freopen(path, "a", stderr)) setvbuf(stderr, NULL, _IONBF, 0);
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    kz_log_setup();
#endif
    kz_pc_settings_env();   /* pcoptions.cpp: kz1hd_pc.ini */
    /* The game steps its command buffer through 64 KB segments with its own
     * callback: the runtime's recycling of a title's ring would rewind it
     * mid-step (a frozen level load; cellGcmSys.c, GCM_RING_RECYCLE). */
    setenv("GCM_RING_RECYCLE", "0", 0);
    kz_pgo_dump_start();
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }
#ifdef __linux__
    /* KZ_PTRACER=1: any process may attach (gdb -p, eu-stack) despite ptrace_scope 1. */
    if (getenv("KZ_PTRACER")) prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif
    kz_prof_start();   /* KZ_PROF, prof.cpp */

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);
    /* The title id (NPUA80856) names the save directory, as on the PS3. */
    { std::string sfo = std::string(ppu_vfs_root) + "/PS3_GAME/PARAM.SFO";
      cellGame_init_from_paramsfo(sfo.c_str()); }

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
#ifndef _WIN32
    { struct sigaction sa = {}; sa.sa_handler = on_quit_signal;
      sigaction(SIGTERM, &sa, NULL); sigaction(SIGINT, &sa, NULL); sigaction(SIGHUP, &sa, NULL); }
#endif
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
