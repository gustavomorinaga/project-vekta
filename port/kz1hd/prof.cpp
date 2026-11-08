/*
 * KZ_PROF="<start s>,<seconds>[,<out file>]": an in-process sampling profiler
 * (start 0: when gameplay is on screen, see main.cpp; start "key": each time
 * F11 is pressed in the game's window or the trigger file appears,
 * KZ_PROF_TRIGGER=<file>, default <out file>.start; prof_trigger.h).
 * At <start> it arms a per-thread CPU-time timer on every thread (1 kHz of
 * that thread's own CPU time), records the interrupted instruction pointer,
 * and after <seconds> writes "tid name rip" lines to <out file> (default
 * kz_prof.txt in the working directory; KZ_PROF_TS=1 appends t<monotonic ns>). tools/prof_report.py turns
 * them into a per-thread, per-function table. KZ_PROF_WALL=1 samples wall
 * time instead (blocked threads too) and adds the call stack, so a thread
 * parked in libc is charged to the game/runtime function that waited; KZ_PROF_STACK=1 adds it to
 * CPU-time samples. No perf on this machine, and
 * eu-stack takes a few samples a second at best.
 */
#ifdef __linux__
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <execinfo.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "prof_trigger.h"   /* KZ_PROF parsing, the trigger file */

extern "C" __thread int g_spurs_cur_wid;   /* cellSpurs.c: workload being run, -1 if none */

namespace {
constexpr unsigned kMax = 1u << 19;
constexpr int kDepth = 48;
struct Sample { int tid; int wid; uintptr_t rip; uint64_t ns; void* bt[kDepth]; };
bool g_wall;
bool g_stack;   /* KZ_PROF_STACK=1: the call stack with each CPU-time sample too */
bool g_ts;   /* KZ_PROF_TS=1: each line also carries t<monotonic ns> */
Sample* g_samples;
std::atomic<unsigned> g_n{0};

void on_prof(int, siginfo_t*, void* uc)
{
    const unsigned i = g_n.fetch_add(1, std::memory_order_relaxed);
    if (i >= kMax) return;
    g_samples[i].tid = (int)syscall(SYS_gettid);
    g_samples[i].rip = (uintptr_t)((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP];
    g_samples[i].wid = g_spurs_cur_wid;
    { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); g_samples[i].ns = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
    if (g_wall || g_stack) backtrace(g_samples[i].bt, kDepth);
}

std::atomic<int> g_armed{0};

/* F11 in the game's window: SDL's keyboard state, which the backend's event
 * loop keeps (SDL2 is linked through ps3recomp; weak, so a build without it
 * still has the trigger file). SDL_SCANCODE_F11 is 68. */
extern "C" const unsigned char* SDL_GetKeyboardState(int*) __attribute__((weak));
bool f11_down()
{
    if (!SDL_GetKeyboardState) return false;
    const unsigned char* k = SDL_GetKeyboardState(nullptr);
    return k && k[68];
}

/* KZ_PROF="key,...": wait for F11 (a new press) or the trigger file. */
const char* wait_trigger(const char* path)
{
    bool was_down = f11_down();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (prof_trigger_fired(path)) return "the trigger file";
        const bool down = f11_down();
        if (down && !was_down) return "F11";
        was_down = down;
    }
}

void record(double secs, const std::string& out);

void run(prof_spec p, std::string trigger)
{
    if (!p.on_key) {
        if (p.start > 0) std::this_thread::sleep_for(std::chrono::duration<double>(p.start));
        else while (!g_armed.load()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        record(p.secs, p.out);
        return;
    }
    fprintf(stderr, "[kz-prof] waiting for F11 or %s to record %.0f s\n", trigger.c_str(), p.secs);
    for (int i = 0;; i++) {
        const char* why = wait_trigger(trigger.c_str());
        /* each recording to its own file: <out>, then <out>.2, .3 ... */
        const std::string out = i ? p.out + ("." + std::to_string(i + 1)) : std::string(p.out);
        fprintf(stderr, "[kz-prof] started by %s\n", why);
        g_n = 0;
        record(p.secs, out);
    }
}

void record(double secs, const std::string& out)
{
    struct sigaction sa = {};
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, nullptr);
    timer_t timers[512]; int nt = 0;
    const int self = (int)syscall(SYS_gettid);
    if (DIR* d = opendir("/proc/self/task")) {
        while (dirent* e = readdir(d)) {
            const int tid = atoi(e->d_name);
            if (tid <= 0 || tid == self || nt >= 512) continue;
            sigevent sev = {};
            sev.sigev_notify = SIGEV_THREAD_ID;
            sev.sigev_signo = SIGPROF;
            sev._sigev_un._tid = tid;
            const clockid_t clk = g_wall ? CLOCK_MONOTONIC
                                         : (clockid_t)((~(unsigned)tid << 3) | 6);   /* that thread's CPU clock */
            if (timer_create(clk, &sev, &timers[nt]) == 0) {
                itimerspec its = {{0, 1000000}, {0, 1000000}};
                timer_settime(timers[nt], 0, &its, nullptr);
                nt++;
            }
        }
        closedir(d);
    }
    fprintf(stderr, "[kz-prof] sampling %d threads for %.0f s\n", nt, secs);
    std::this_thread::sleep_for(std::chrono::duration<double>(secs));
    for (int i = 0; i < nt; i++) timer_delete(timers[i]);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const unsigned n = std::min(g_n.load(), kMax);
    FILE* f = fopen(out.c_str(), "w");
    if (!f) return;
    for (unsigned i = 0; i < n; i++) {
        char path[64], name[32] = "?";
        snprintf(path, sizeof path, "/proc/self/task/%d/comm", g_samples[i].tid);
        static int last_tid = -1; static char last_name[32];
        if (g_samples[i].tid == last_tid) strcpy(name, last_name);
        else if (FILE* c = fopen(path, "r")) {
            if (fgets(name, sizeof name, c)) name[strcspn(name, "\n ")] = 0;
            fclose(c);
            last_tid = g_samples[i].tid; strcpy(last_name, name);
        }
        /* SPURS threads: the thread name carries the workload, "KZ1HD/w1" */
        char tn[48];
        if (g_samples[i].wid >= 0) snprintf(tn, sizeof tn, "%s/w%d", name, g_samples[i].wid);
        else snprintf(tn, sizeof tn, "%s", name);
        fprintf(f, "%d %s %lx", g_samples[i].tid, tn, (unsigned long)g_samples[i].rip);
        if (g_ts) fprintf(f, " t%llu", (unsigned long long)g_samples[i].ns);
        for (int k = 0; (g_wall || g_stack) && k < kDepth && g_samples[i].bt[k]; k++)
            fprintf(f, " %lx", (unsigned long)g_samples[i].bt[k]);
        fputc('\n', f);
    }
    fclose(f);
    /* the mappings, for the report to place each address */
    if (FILE* m = fopen("/proc/self/maps", "r")) {
        if (FILE* o = fopen((out + ".maps").c_str(), "w")) {
            char line[1024];
            while (fgets(line, sizeof line, m)) fputs(line, o);
            fclose(o);
        }
        fclose(m);
    }
    fprintf(stderr, "[kz-prof] stopped after %.0f s: %u samples -> %s\n", secs, n, out.c_str());
}
}  // namespace

/* KZ_PROF="0,<seconds>,...": sampling waits for this call (main.cpp makes it
 * once gameplay is on screen). */
extern "C" void kz_prof_arm(void) { g_armed = 1; }

extern "C" void kz_prof_start(void)
{
    const char* e = getenv("KZ_PROF");
    if (!e) return;
    prof_spec p;
    if (!prof_parse(e, &p)) { fprintf(stderr, "[kz-prof] KZ_PROF=\"%s\" not understood (<start>|key,<seconds>[,<file>])\n", e); return; }
    char trigger[600];
    prof_trigger_path(&p, getenv("KZ_PROF_TRIGGER"), trigger, sizeof trigger);
    g_wall = getenv("KZ_PROF_WALL") != nullptr;
    g_ts = getenv("KZ_PROF_TS") != nullptr;
    g_stack = getenv("KZ_PROF_STACK") != nullptr;
    if (g_wall) { void* warm[1]; backtrace(warm, 1); }   /* loads libgcc_s outside the handler */
    g_samples = (Sample*)calloc(kMax, sizeof(Sample));
    std::thread(run, p, std::string(trigger)).detach();
}
#else
extern "C" void kz_prof_start(void) {}
extern "C" void kz_prof_arm(void) {}
#endif
