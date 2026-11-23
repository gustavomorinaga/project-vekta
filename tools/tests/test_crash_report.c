/*
 * Host test for port/kz1hd/crash_report.c and sys_info.c (tools/tests/test_crash_report.sh on Linux; the Windows build
 * runs under Wine in tools/wine-host-tests.sh). Linked with ps3recomp's ps3_stdio.c (and win32_compat.c off Windows),
 * the same filter chain as the game: kz_crash_install first, ps3_stdio_init's filter over it.
 *
 *   test_crash_report <folder>          the checks: runs itself as a child once per case, stderr into a file
 *   test_crash_report child <kind>      a child: installs everything as main() does, then KZ_CRASH_TEST=<kind>
 *
 * Each crash kind (null-main, null-thread, abort, illegal, stack, fastfail, and two threads faulting at once): the
 * [kz-crash] block and its required lines, the exit status, the lines printed (buffered) before the crash coming out
 * before the block, a fault a vectored handler resolves leaving no report, frame #0 inside the function that faulted
 * (module+offset as this program computes its own), the signature's form and that it is the same on a second run,
 * and the copy in KZ_CRASH_DIR (a folder that does not exist yet) equal to the block. The hang watchdog: two
 * [kz-hang] blocks (at KZ_HANG_WATCH=1 s and 6 s) with every thread's stack and the copies, the game still running
 * after them; no report with KZ_HANG_WATCH=0 or with the window minimized. The session header's lines.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crash_report.h"
#include "sys_info.h"
#include "win32_compat.h"
#include "ps3_stdio.h"
#include "ps3emu/thread_name.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

/* ==================================================================================================================
 * The child
 * ================================================================================================================*/
static volatile unsigned s_flips;
static unsigned flips(void) { return s_flips; }

static DWORD WINAPI frame_loop(LPVOID arg)
{
    (void)arg;
    ps3_thread_name("frame clock");
    const int minimized = getenv("KZ_TEST_MINIMIZED") != NULL;
    for (;;) {
        s_flips++;
        kz_hang_tick(minimized);
        kz_crash_test_hang_point();
        Sleep(16);
    }
    return 0;
}

static DWORD WINAPI worker(LPVOID arg)
{
    (void)arg;
    ps3_thread_name("test worker");
    for (;;) Sleep(1000);
    return 0;
}

static volatile int s_go;
static volatile int* volatile s_nil;
static DWORD WINAPI both(LPVOID arg)
{
    ps3_thread_name(arg ? "crash b" : "crash a");
    while (!s_go) Sleep(0);
    kz_crash_null_write(s_nil);
    return 0;
}

/* A vectored handler that resolves faults in its own arena, as main.cpp's vm_commit_veh does: no report for them. */
static unsigned char* s_arena;
static LONG WINAPI arena_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
    if (!s_arena || a < (uintptr_t)s_arena || a >= (uintptr_t)s_arena + 0x10000) return EXCEPTION_CONTINUE_SEARCH;
#ifdef _WIN32
    if (!VirtualAlloc(s_arena, 0x10000, MEM_COMMIT, PAGE_READWRITE)) return EXCEPTION_CONTINUE_SEARCH;
#else
    if (mprotect(s_arena, 0x10000, PROT_READ | PROT_WRITE) != 0) return EXCEPTION_CONTINUE_SEARCH;
#endif
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int child(const char* kind)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    fprintf(stderr, "[kz] test version line\n");
    kz_crash_install("test version line");
    if (strcmp(kind, "sysinfo") == 0) {
        kz_sys_log();
        char s[256] = "";
        kz_sys_pair(s, sizeof s, "vsync", "fifo");
        kz_sys_pair(s, sizeof s, "language", "system");
        kz_sys_log_line("settings", s);
        kz_sys_log_line("dlss", "no");
        return 0;
    }
#ifdef _WIN32
    s_arena = (unsigned char*)VirtualAlloc(NULL, 0x10000, MEM_RESERVE, PAGE_READWRITE);
#else
    s_arena = (unsigned char*)mmap(NULL, 0x10000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    AddVectoredExceptionHandler(1, arena_veh);
    s_arena[100] = 7;   /* faults once; the handler commits the page and the store runs again */
    fprintf(stderr, "veh ok %d\n", s_arena[100]);

    kz_crash_test_start();
    kz_hang_watch_start(flips);
    HANDLE t = CreateThread(NULL, 0, frame_loop, NULL, 0, NULL);
    if (t) CloseHandle(t);
    t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (t) CloseHandle(t);

    ps3_stdio_init();   /* as ppu_run does, after kz_crash_install: its filter over ours; first call wins */
    fprintf(stderr, "before the crash\n");   /* buffered (PS3_STDIO_FLUSH_MS is long): the crash flush prints it */

    if (strcmp(kind, "both") == 0) {
        HANDLE a = CreateThread(NULL, 0, both, NULL, 0, NULL), b = CreateThread(NULL, 0, both, (LPVOID)1, 0, NULL);
        Sleep(100);
        s_go = 1;
        if (a) CloseHandle(a);
        if (b) CloseHandle(b);
    }
    kz_crash_test_main_point();   /* the main-thread kinds fire here */
    for (int i = 0; i < 600; i++) Sleep(100);   /* the thread kinds; the parent ends a hang */
    fprintf(stderr, "child: nothing happened\n");
    return 3;
}

/* ==================================================================================================================
 * The checks
 * ================================================================================================================*/
static int fails;
static void check(int ok, const char* what, const char* detail)
{
    printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", what, detail && *detail ? ": " : "", detail ? detail : "");
    if (!ok) fails++;
}

static char s_self[1024], s_exe[1024], s_root[1024];

static char* read_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc((size_t)n + 1);
    size_t g = fread(b, 1, (size_t)n, f);
    b[g] = 0;
    fclose(f);
    /* Windows' text files: drop the CRs */
    char* w = b;
    for (char* r = b; *r; r++) if (*r != '\r') *w++ = *r;
    *w = 0;
    return b;
}

static void set_env(const char* name, const char* value)
{
#ifdef _WIN32
    SetEnvironmentVariableA(name, value);
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}

static void mk(const char* d)
{
#ifdef _WIN32
    CreateDirectoryA(d, NULL);
#else
    mkdir(d, 0777);
#endif
}

/* The files in dir whose names start with prefix: how many, and the first one's path. */
static int count_files(const char* dir, const char* prefix, char* first, size_t cap)
{
    int n = 0;
#ifdef _WIN32
    char pat[1200];
    snprintf(pat, sizeof pat, "%s\\%s*", dir, prefix);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!n && first) snprintf(first, cap, "%s\\%s", dir, fd.cFileName);
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    if (!d) return 0;
    for (struct dirent* e; (e = readdir(d));) {
        if (strncmp(e->d_name, prefix, strlen(prefix)) != 0) continue;
        if (!n && first) snprintf(first, cap, "%s/%s", dir, e->d_name);
        n++;
    }
    closedir(d);
#endif
    return n;
}

typedef struct { int exited, code, signaled, sig, timed_out, alive_at_end; } kz_status;

/* Runs "self child kind" with stderr and stdout into log. watch_dir: end it once two hang-*.txt are there (or at
 * timeout), noting whether it was still running. */
static kz_status run_child(const char* kind, const char* log, int timeout_ms, const char* watch_dir, int min_ms)
{
    kz_status st;
    memset(&st, 0, sizeof st);
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE lf = CreateFileA(log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = lf;
    si.hStdInput = NULL;
    PROCESS_INFORMATION pi;
    char cmd[1400];
    snprintf(cmd, sizeof cmd, "\"%s\" child %s", s_self, kind);
    if (!CreateProcessA(s_self, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(lf);
        st.timed_out = 1;
        return st;
    }
    CloseHandle(lf);
    int waited = 0;
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) break;
        waited += 50;
        if ((watch_dir && waited >= min_ms && count_files(watch_dir, "hang-", NULL, 0) >= 2) ||
            (!watch_dir && min_ms && waited >= min_ms) || waited >= timeout_ms) {
            st.alive_at_end = 1;
            if (waited >= timeout_ms && watch_dir) st.timed_out = 1;
            if (!watch_dir && !min_ms) st.timed_out = 1;
            Sleep(300);   /* the second report's file is written after its last line */
            TerminateProcess(pi.hProcess, 99);
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    st.exited = 1;
    st.code = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    pid_t p = fork();
    if (p == 0) {
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 1); dup2(fd, 2);
        close(fd);
        execl(s_self, s_self, "child", kind, (char*)NULL);
        _exit(127);
    }
    int waited = 0, ws = 0;
    for (;;) {
        if (waitpid(p, &ws, WNOHANG) == p) break;
        usleep(50000);
        waited += 50;
        if ((watch_dir && waited >= min_ms && count_files(watch_dir, "hang-", NULL, 0) >= 2) ||
            (!watch_dir && min_ms && waited >= min_ms) || waited >= timeout_ms) {
            st.alive_at_end = waitpid(p, &ws, WNOHANG) == 0;
            if (waited >= timeout_ms && watch_dir) st.timed_out = 1;
            if (!watch_dir && !min_ms) st.timed_out = 1;
            usleep(300000);
            kill(p, SIGKILL);
            waitpid(p, &ws, 0);
            ws = -1;
            break;
        }
    }
    if (ws != -1) {
        st.exited = WIFEXITED(ws);
        st.code = st.exited ? WEXITSTATUS(ws) : 0;
        st.signaled = WIFSIGNALED(ws);
        st.sig = st.signaled ? WTERMSIG(ws) : 0;
    }
#endif
    return st;
}

/* The lines of the log's blocks with prefix ("[kz-crash] "), and how many blocks. */
static int blocks(const char* log, const char* prefix, char* out, size_t cap)
{
    int n = 0;
    size_t o = 0, pl = strlen(prefix);
    out[0] = 0;
    for (const char* l = log; *l;) {
        const char* e = strchr(l, '\n');
        size_t len = e ? (size_t)(e - l) : strlen(l);
        if (len >= pl && strncmp(l, prefix, pl) == 0) {
            if (len - pl == 5 && strncmp(l + pl, "begin", 5) == 0) n++;
            if (o + len - pl + 2 < cap) { memcpy(out + o, l + pl, len - pl); o += len - pl; out[o++] = '\n'; out[o] = 0; }
        }
        if (!e) break;
        l = e + 1;
    }
    return n;
}

/* The value after "key: " on the block's first such line, into v. */
static int value(const char* block, const char* key, char* v, size_t cap)
{
    size_t kl = strlen(key);
    for (const char* l = block; *l;) {
        const char* e = strchr(l, '\n');
        size_t len = e ? (size_t)(e - l) : strlen(l);
        if (len > kl + 1 && strncmp(l, key, kl) == 0 && l[kl] == ':' && l[kl + 1] == ' ') {
            size_t n = len - kl - 2 < cap - 1 ? len - kl - 2 : cap - 1;
            memcpy(v, l + kl + 2, n); v[n] = 0;
            return 1;
        }
        if (!e) break;
        l = e + 1;
    }
    v[0] = 0;
    return 0;
}

static int has_line(const char* block, const char* line)
{
    size_t n = strlen(line);
    for (const char* l = block; *l;) {
        const char* e = strchr(l, '\n');
        size_t len = e ? (size_t)(e - l) : strlen(l);
        if (len == n && strncmp(l, line, n) == 0) return 1;
        if (!e) break;
        l = e + 1;
    }
    return 0;
}

static int frames_count(const char* block)
{
    int n = 0;
    for (const char* l = block; *l;) {
        if (l[0] == '#' && l[1] >= '0' && l[1] <= '9') n++;
        const char* e = strchr(l, '\n');
        if (!e) break;
        l = e + 1;
    }
    return n;
}

static int sig_form(const char* s)   /* KZ-XXXX-XXXX, upper-case hex */
{
    if (strlen(s) != 12 || strncmp(s, "KZ-", 3) != 0 || s[7] != '-') return 0;
    for (int i = 3; i < 12; i++) {
        if (i == 7) continue;
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F'))) return 0;
    }
    return 1;
}

/* This program's own offset of f in its module, as the report computes it. */
static uintptr_t own_offset(const void* f)
{
#ifdef _WIN32
    return (uintptr_t)f - (uintptr_t)GetModuleHandleA(NULL);
#else
    Dl_info info;
    struct link_map* lm = NULL;
    if (!dladdr1(f, &info, (void**)&lm, RTLD_DL_LINKMAP) || !lm) return 0;
    return (uintptr_t)f - lm->l_addr;
#endif
}

typedef struct {
    const char* kind;
    const char* head;          /* the exception or signal line */
    const char* access;        /* "read", "write" or NULL */
    const char* thread;        /* the thread's name or NULL */
    const void* fn;            /* frame #0 is in this function, or NULL */
    int         sig;           /* Linux: the signal it ends by */
    int         codes[3];      /* Windows: the exit codes it may end with */
} kz_case;

static char s_sigs[8][16];

static void check_crash(const kz_case* c, int run, char* sig_out)
{
    char what[256], d[2048], dir[1200], log[1200];
    snprintf(dir, sizeof dir, "%s/crash-%s-%d", s_root, c->kind, run);
    snprintf(log, sizeof log, "%s/%s-%d.log", s_root, c->kind, run);
    set_env("KZ_CRASH_TEST", strcmp(c->kind, "both") == 0 ? NULL : c->kind);
    set_env("KZ_CRASH_DIR", dir);
    kz_status st = run_child(c->kind, log, 30000, NULL, 0);
    char* text = read_file(log);
    if (!text) text = strdup("");
    static char blk[64 * 1024];
    const int nb = blocks(text, "[kz-crash] ", blk, sizeof blk);
    const int fast = strcmp(c->kind, "fastfail") == 0;

    snprintf(what, sizeof what, "%s (run %d): ", c->kind, run);
    char w2[600];
#define W(s) (snprintf(w2, sizeof w2, "%s%s", what, s), w2)
    check(!st.timed_out, W("the child ended by itself"), NULL);
    check(strstr(text, "veh ok 7") != NULL, W("a fault the vectored handler resolves goes on (veh ok 7)"), NULL);
#ifdef _WIN32
    int code_ok = 0;
    for (int i = 0; i < 3; i++) if (c->codes[i] && (unsigned)st.code == (unsigned)c->codes[i]) code_ok = 1;
    snprintf(d, sizeof d, "exit 0x%08X", (unsigned)st.code);
    check(st.exited && code_ok, W("the exit status"), d);
#else
    snprintf(d, sizeof d, "exited %d code %d, signaled %d signal %d", st.exited, st.code, st.signaled, st.sig);
    check(st.signaled && st.sig == c->sig, W("ends by its signal"), d);
#endif
    if (fast) {
#ifdef _WIN32
        check(nb <= 1, W("no report or one (__fastfail runs no handler on Windows; Wine may raise it)"), NULL);
#else
        check(nb == 0, W("no report (nothing in the process sees it)"), NULL);
#endif
        free(text);
        return;
    }
    snprintf(d, sizeof d, "%d", nb);
    check(nb == 1, W("one [kz-crash] block"), d);
    check(has_line(blk, "begin") && has_line(blk, "end"), W("begin and end"), NULL);
    {
        const char* b = strstr(text, "before the crash");
        const char* k = strstr(text, "[kz-crash] begin");
        check(b && k && b < k, W("what was printed (buffered) before the crash comes out before the block"), NULL);
    }
    check(has_line(blk, c->head), W(c->head), NULL);
    char v[512];
    check(value(blk, "address", v, sizeof v) && strncmp(v, "0x", 2) == 0, W("address: 0x..."), v);
    if (c->access) {
        char want[64];
        snprintf(want, sizeof want, "access: %s 0x0000000000000000", c->access);
        check(has_line(blk, want), W(want), NULL);
    }
    check(value(blk, "module", v, sizeof v) && strstr(v, "+0x"), W("module: <name>+0x<offset>"), v);
    if (value(blk, "thread", v, sizeof v)) {
        const char* sp = strchr(v, ' ');
        int digits = sp && sp > v;
        for (const char* q = v; q < sp && digits; q++) digits = *q >= '0' && *q <= '9';
        check(digits && (!c->thread || strcmp(sp + 1, c->thread) == 0), W("thread: <id> <name>"), v);
    } else {
        check(0, W("thread: line"), NULL);
    }
    check(value(blk, "uptime", v, sizeof v) && strstr(v, " s"), W("uptime: <seconds> s"), v);
    check(value(blk, "version", v, sizeof v) && strcmp(v, "test version line") == 0, W("version:"), v);
#if defined(__x86_64__)
    if (strcmp(c->kind, "abort") != 0)   /* abort() has no fault context */
        check(value(blk, "registers", v, sizeof v) && strstr(v, "rip=0x"), W("registers:"), v);
#endif
    const int nf = frames_count(blk);
    snprintf(d, sizeof d, "%d frames", nf);
    check(nf >= 2 && nf <= 32, W("2 to 32 frames"), d);
    {
        char f0[1100];
        snprintf(f0, sizeof f0, "#0 %s+0x", s_exe);
        if (strcmp(c->kind, "abort") == 0) {   /* #0 is in the C library (raise), its caller here */
            snprintf(f0, sizeof f0, " %s+0x", s_exe);
            check(strstr(blk, f0) != NULL, W("a frame in this program (abort's caller)"), NULL);
        } else {
            check(strstr(blk, f0) != NULL, W("frame #0 in this program"), NULL);
        }
        if (c->fn) {
            const char* p = strstr(blk, f0);
            unsigned long long off = p ? strtoull(p + strlen(f0), NULL, 16) : 0;
            const uintptr_t fo = own_offset(c->fn);
            snprintf(d, sizeof d, "frame #0 at +0x%llx, the function at +0x%llx", off, (unsigned long long)fo);
            check(p && off >= fo && off < fo + 64, W("frame #0's offset is inside the function that faulted"), d);
        }
        if (strcmp(c->kind, "stack") == 0) {
            snprintf(f0, sizeof f0, "#20 %s+0x", s_exe);
            check(strstr(blk, f0) != NULL, W("frame #20 still in this program (the recursion)"), NULL);
        }
    }
    check(value(blk, "signature", v, sizeof v) && sig_form(v), W("signature: KZ-XXXX-XXXX"), v);
    snprintf(sig_out, 16, "%s", v);
    {
        char first[1300];
        const int nfile = count_files(dir, "crash-", first, sizeof first);
        snprintf(d, sizeof d, "%d file(s) in %s", nfile, dir);
        check(nfile == 1, W("one crash-*.txt in KZ_CRASH_DIR (created)"), d);
        if (nfile == 1) {
            const char* base = first + strlen(dir) + 1;
            int form = strlen(base) == strlen("crash-YYYYmmdd-HHMMSS.txt") && base[14] == '-' &&
                       strcmp(base + 21, ".txt") == 0;
            check(form, W("named crash-YYYYmmdd-HHMMSS.txt"), base);
            char* copy = read_file(first);
            check(copy && strcmp(copy, blk) == 0, W("the copy is the block without the prefix"), NULL);
            free(copy);
        }
    }
#undef W
    free(text);
}

static void check_hang(void)
{
    char dir[1200], log[1200], d[512];
    snprintf(dir, sizeof dir, "%s/hang", s_root);
    snprintf(log, sizeof log, "%s/hang.log", s_root);
    set_env("KZ_CRASH_TEST", "hang");
    set_env("KZ_CRASH_TEST_DELAY", "0.5");
    set_env("KZ_HANG_WATCH", "1");
    set_env("KZ_CRASH_DIR", dir);
    kz_status st = run_child("hang", log, 25000, dir, 7000);
    char* text = read_file(log);
    if (!text) text = strdup("");
    static char blk[512 * 1024];
    const int nb = blocks(text, "[kz-hang] ", blk, sizeof blk);
    check(!st.timed_out, "hang: two reports within 25 s", NULL);
    check(st.alive_at_end, "hang: the game was still running after both (the watchdog never stops it)", NULL);
    snprintf(d, sizeof d, "%d", nb);
    check(nb == 2, "hang: two [kz-hang] blocks", d);
    check(strstr(text, "[kz-hang] begin\n[kz-hang] no new frame for 1 s\n") != NULL,
          "hang: the first: \"no new frame for 1 s\" right after begin", NULL);
    check(strstr(text, "[kz-hang] begin\n[kz-hang] no new frame for 6 s\n") != NULL,
          "hang: the second at six times as long: \"no new frame for 6 s\"", NULL);
    check(strstr(blk, " frame clock\n#0 ") != NULL, "hang: the stopped frame loop's thread, with frames", NULL);
    check(strstr(blk, " test worker\n#0 ") != NULL, "hang: another thread (test worker), with frames", NULL);
    check(strstr(blk, " main\n#0 ") != NULL, "hang: the main thread, with frames", NULL);
    check(strstr(blk, "kz hang watch") == NULL, "hang: not the watchdog itself", NULL);
    {
        char f[1100];
        snprintf(f, sizeof f, " %s+0x", s_exe);
        check(strstr(blk, f) != NULL, "hang: frames in this program", NULL);
    }
    char first[1300];
    const int nfile = count_files(dir, "hang-", first, sizeof first);
    snprintf(d, sizeof d, "%d", nfile);
    check(nfile == 2, "hang: two hang-*.txt in KZ_CRASH_DIR", d);
    if (nfile >= 1) {
        char* copy = read_file(first);
        check(copy && strncmp(copy, "begin\nno new frame for ", 23) == 0 && strstr(copy, "\nend\n") &&
              strstr(blk, copy) != NULL, "hang: a copy is one of the blocks, without the prefix", NULL);
        free(copy);
    }
    free(text);

    /* KZ_HANG_WATCH=0: no watchdog */
    snprintf(dir, sizeof dir, "%s/hang-off", s_root);
    snprintf(log, sizeof log, "%s/hang-off.log", s_root);
    set_env("KZ_HANG_WATCH", "0");
    set_env("KZ_CRASH_DIR", dir);
    st = run_child("hang", log, 30000, NULL, 2500);
    text = read_file(log);
    if (!text) text = strdup("");
    check(strstr(text, "[kz-hang]") == NULL && strstr(text, "hang watch: off") != NULL,
          "hang: KZ_HANG_WATCH=0: no report, and the log says the watch is off", NULL);
    free(text);

    /* minimized: not a hang */
    snprintf(dir, sizeof dir, "%s/hang-min", s_root);
    snprintf(log, sizeof log, "%s/hang-min.log", s_root);
    set_env("KZ_HANG_WATCH", "1");
    set_env("KZ_TEST_MINIMIZED", "1");
    set_env("PS3_STDIO_FLUSH_MS", "50");   /* the child is killed: its last lines must be out by then */
    st = run_child("hang", log, 30000, NULL, 3000);
    text = read_file(log);
    if (!text) text = strdup("");
    check(strstr(text, "[kz-hang]") == NULL && strstr(text, "the frame loop stops now") != NULL,
          "hang: the window minimized: no report", NULL);
    free(text);
    set_env("KZ_TEST_MINIMIZED", NULL);
    set_env("PS3_STDIO_FLUSH_MS", "10000");
    set_env("KZ_HANG_WATCH", NULL);
    set_env("KZ_CRASH_TEST", NULL);
    set_env("KZ_CRASH_TEST_DELAY", NULL);
    (void)st;
}

static void check_sysinfo(void)
{
    char log[1200], v[2048];
    snprintf(log, sizeof log, "%s/sysinfo.log", s_root);
    set_env("KZ_CRASH_DIR", NULL);
    set_env("KZ_STARTED_BY", "zip");
    set_env("KZ_TEST_MARK", "a b");
    const char* home = getenv(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    char hp[1200] = "";
    if (home && strlen(home) > 1) { snprintf(hp, sizeof hp, "%s%cvekta", home, '/'); set_env("PS3_TEST_PATH", hp); }
    run_child("sysinfo", log, 30000, NULL, 0);
    char* text = read_file(log);
    if (!text) text = strdup("");
    static char blk[64 * 1024];
    blocks(text, "[kz-sys] ", blk, sizeof blk);
    check(strncmp(text, "[kz] test version line\n[kz-sys] os: ", 36) == 0, "sys: os right after the version line", NULL);
    const char* keys[] = { "os", "cpu", "cores", "memory", "env", "started-by", "settings", "dlss" };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        char w[64];
        snprintf(w, sizeof w, "sys: [kz-sys] %s:", keys[i]);
        check(value(blk, keys[i], v, sizeof v) && v[0], w, v);
    }
    value(blk, "env", v, sizeof v);
    check(strstr(v, "KZ_TEST_MARK=\"a b\"") != NULL, "sys: env quotes a value with a space", v);
    check(strstr(v, "KZ_STARTED_BY=zip") != NULL, "sys: env holds the KZ_* variables", v);
    if (hp[0]) check(strstr(v, "PS3_TEST_PATH=~/vekta") != NULL, "sys: env holds PS3* and writes the home folder ~", v);
    value(blk, "started-by", v, sizeof v);
    check(strcmp(v, "zip") == 0, "sys: started-by from KZ_STARTED_BY", v);
    value(blk, "settings", v, sizeof v);
    check(strcmp(v, "vsync=fifo language=system") == 0, "sys: settings as name=value pairs", v);
    free(text);
    set_env("KZ_STARTED_BY", NULL);
    run_child("sysinfo", log, 30000, NULL, 0);
    text = read_file(log);
    if (!text) text = strdup("");
    blocks(text, "[kz-sys] ", blk, sizeof blk);
    value(blk, "started-by", v, sizeof v);
    check(strcmp(v, "unknown") == 0, "sys: started-by unknown without KZ_STARTED_BY", v);
    free(text);
    set_env("KZ_TEST_MARK", NULL);
    set_env("PS3_TEST_PATH", NULL);
}

int main(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "child") == 0) return child(argv[2]);
    if (argc != 2) { fprintf(stderr, "usage: %s <empty folder>\n", argv[0]); return 2; }
    snprintf(s_root, sizeof s_root, "%s", argv[1]);
    mk(s_root);
#ifdef _WIN32
    GetModuleFileNameA(NULL, s_self, sizeof s_self);
#else
    ssize_t n = readlink("/proc/self/exe", s_self, sizeof s_self - 1);
    if (n > 0) s_self[n] = 0;
#endif
    const char* b = s_self;
    for (const char* c = s_self; *c; c++) if (*c == '/' || *c == '\\') b = c + 1;
    snprintf(s_exe, sizeof s_exe, "%s", b);

    set_env("PS3_STDIO_FLUSH_MS", "10000");   /* the timer never flushes in time: only the crash flush can */
    set_env("PS3_STDIO_UNBUFFERED", NULL);
    set_env("KZ_HANG_WATCH", NULL);
    set_env("KZ_CRASH_TEST_DELAY", "0.3");
    set_env("KZ_CRASH_WER", NULL);
#ifdef _WIN32
    const kz_case cases[] = {
        { "null-main", "exception: 0xC0000005 EXCEPTION_ACCESS_VIOLATION", "read", "main", (const void*)kz_crash_null_read, 0, { (int)0xC0000005u } },
        { "null-thread", "exception: 0xC0000005 EXCEPTION_ACCESS_VIOLATION", "write", "kz crash test", (const void*)kz_crash_null_write, 0, { (int)0xC0000005u } },
        { "abort", "signal: 22 SIGABRT", NULL, "main", NULL, 0, { 3, (int)0xC0000409u, 0x40000015 } },
        { "illegal", "exception: 0xC000001D EXCEPTION_ILLEGAL_INSTRUCTION", NULL, "main", NULL, 0, { (int)0xC000001Du } },
        { "stack", "exception: 0xC00000FD EXCEPTION_STACK_OVERFLOW", NULL, "main", NULL, 0, { (int)0xC00000FDu } },
        { "fastfail", NULL, NULL, NULL, NULL, 0, { (int)0xC0000409u } },
        { "both", "exception: 0xC0000005 EXCEPTION_ACCESS_VIOLATION", "write", NULL, (const void*)kz_crash_null_write, 0, { (int)0xC0000005u } },
    };
#else
    const kz_case cases[] = {
        { "null-main", "signal: 11 SIGSEGV", "read", "main", (const void*)kz_crash_null_read, SIGSEGV, { 0 } },
        { "null-thread", "signal: 11 SIGSEGV", "write", "kz crash test", (const void*)kz_crash_null_write, SIGSEGV, { 0 } },
        { "abort", "signal: 6 SIGABRT", NULL, "main", NULL, SIGABRT, { 0 } },
        { "illegal", "signal: 4 SIGILL", NULL, "main", NULL, SIGILL, { 0 } },
        { "stack", "signal: 11 SIGSEGV", NULL, "main", NULL, SIGSEGV, { 0 } },
        { "fastfail", NULL, NULL, NULL, NULL, SIGKILL, { 0 } },
        { "both", "signal: 11 SIGSEGV", "write", NULL, (const void*)kz_crash_null_write, SIGSEGV, { 0 } },
    };
#endif
    const int nc = (int)(sizeof cases / sizeof *cases);
    for (int i = 0; i < nc; i++) {
        check_crash(&cases[i], 1, s_sigs[i]);
        if (cases[i].head && strcmp(cases[i].kind, "both") != 0) {
            char again[16] = "";
            check_crash(&cases[i], 2, again);
            char w[128], d[160];
            snprintf(w, sizeof w, "%s: the same signature on a second run", cases[i].kind);
            snprintf(d, sizeof d, "%s, %s", s_sigs[i], again);
            check(s_sigs[i][0] && strcmp(s_sigs[i], again) == 0, w, d);
        }
    }
    check(strcmp(s_sigs[0], s_sigs[1]) != 0, "null-main and null-thread have different signatures", NULL);
    check_hang();
    check_sysinfo();
    printf("test_crash_report: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
