/*
 * Crash reports, the hang watchdog and KZ_CRASH_TEST (crash_report.h; the formats: docs/dev/crash-reports.md).
 *
 * Why: issue #129 and a second report showed the game dying a few seconds into Level 1 with nothing in the log. The
 * runtime buffers stdout and stderr (runtime/platform/ps3_stdio.c) and its unhandled-exception filter only flushes
 * them, so a crash left no trace of where it happened.
 *
 * The filter chain. kz_crash_install() runs early in main() and calls SetUnhandledExceptionFilter; ppu_run's
 * ps3_stdio_init() installs ps3_stdio_on_crash later, keeps this filter as its previous one and calls it between
 * its two flushes. So a crash prints what was buffered, then this report, then whatever was buffered after. On
 * Linux SetUnhandledExceptionFilter is win32_compat.c's: its SIGSEGV/SIGBUS/SIGILL/SIGFPE dispatcher runs the
 * vectored handlers first (vm_commit_veh in main.cpp, the guard pages, the write tracking of patch 3031), and only a
 * fault none of them resolved reaches the filter. abort() is a SIGABRT handler (ps3_stdio's chains to it the same
 * way, through signal()).
 *
 * In the filter: no stdio and no heap where it can be helped. Lines are formatted by hand into fixed buffers and
 * written with write() / WriteFile to stderr's descriptor, one write per line, so a crash on a thread that holds the
 * stdio lock still reports, and a deadlock half way (the unwinder, the loader lock) still leaves the lines before
 * it. A second fault on another thread waits for the first report; a fault inside the report ends the process as it
 * would have. On Linux the unwinder runs with the fault signals unblocked and a sigsetjmp to come back to, so a
 * corrupt stack cuts the frames short instead of killing the report.
 *
 * Unwinding. Windows: RtlLookupFunctionEntry / RtlVirtualUnwind from the exception's CONTEXT (a leaf with no unwind
 * data pops its return address). Linux: _Unwind_Backtrace from inside the filter, which walks through the signal
 * trampoline; the frames start at the one whose address is the faulting PC. A PC outside any mapping (a call through
 * a null pointer) has no unwind data, so that case takes the return address at the stack pointer and then the frame
 * pointer chain.
 *
 * After the report the filter returns to the chain. On Windows it then ends the process with the exception code as
 * its exit status (TerminateProcess), the status Windows would have given it, without the Windows Error Reporting
 * dialog; KZ_CRASH_WER=1 hands it to Windows instead. On Linux the dispatcher restores the default action and the
 * fault re-runs, so the process still ends by its signal.
 *
 * Written for this port from the Win32 and Linux documentation; nothing is taken from RPCS3 or another project.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "crash_report.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win32_compat.h"   /* SetUnhandledExceptionFilter, CreateThread, Sleep: the real ones on Windows */
#include "ps3_stdio.h"      /* ps3_stdio_init, ps3_stdio_flush_crash */
#include "ps3emu/thread_name.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#endif

#define KZ_MAX_FRAMES  32
#define KZ_SIG_FRAMES  5           /* the frames the signature hashes, with the code */
#define KZ_LINE        640

/* The render breadcrumbs (ps3recomp's Vulkan backend, from the vk-diagnostics patches): the last GPU passes, one per
 * line. Weak: a build without them reports without render: lines. */
extern int rsx_vulkan_breadcrumbs(char* buf, size_t n) __attribute__((weak));

/* ------------------------------------------------------------------------------------------------------------------
 * State set at install
 * ----------------------------------------------------------------------------------------------------------------*/
static char     s_version[256];
static char     s_exe_name[128] = "KZ1HD";
static uint64_t s_start_us;
static unsigned long s_main_tid;
static LPTOP_LEVEL_EXCEPTION_FILTER s_prev_filter;
static volatile unsigned long s_crashing;   /* the thread reporting a crash, 0 = none */
static volatile int s_crash_done;
static unsigned long s_watch_tid;           /* the hang watchdog, which skips itself */

#ifdef _WIN32
static HANDLE   s_err_h = INVALID_HANDLE_VALUE;
static int      s_err_disk;
static WCHAR    s_dir[1024];                /* KZ_CRASH_DIR */
static HMODULE  s_exe_base;
static int      s_wer;                      /* KZ_CRASH_WER=1 */
static void (*s_prev_abort)(int);
#else
static char     s_dir[1024];
static long     s_gmtoff;                   /* local time = UTC + this, taken at install */
static int      s_probe[2] = { -1, -1 };    /* a pipe: the safe-read fallback */
static __thread sigjmp_buf t_recover;       /* a fault while unwinding comes back here */
static __thread volatile int t_recover_armed;
#endif

static unsigned long cur_tid(void) { return (unsigned long)GetCurrentThreadId(); }

static uint64_t now_us(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e6 / (double)f.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
#endif
}

/* ------------------------------------------------------------------------------------------------------------------
 * Raw output: stderr's descriptor, one write per line
 * ----------------------------------------------------------------------------------------------------------------*/
static void raw_write(const char* s, size_t n)
{
#ifdef _WIN32
    HANDLE h = s_err_h;
    if (!h || h == INVALID_HANDLE_VALUE) return;
    /* kz_log_setup opens the log twice in append mode (stdout and stderr), and the C library seeks to the end before
     * each of its writes; a write of our own must too, or it lands over the other stream's last lines */
    if (s_err_disk) { LARGE_INTEGER z; z.QuadPart = 0; SetFilePointerEx(h, z, NULL, FILE_END); }
    while (n) {
        DWORD w = 0;
        if (!WriteFile(h, s, (DWORD)n, &w, NULL) || !w) return;
        s += w; n -= w;
    }
#else
    while (n) {
        ssize_t w = write(STDERR_FILENO, s, n);
        if (w < 0) { if (errno == EINTR) continue; return; }
        s += w; n -= (size_t)w;
    }
#endif
}

/* One report being written: the line being built, and the copy for the file (the lines without the prefix). */
typedef struct {
    const char* prefix;
    char   line[KZ_LINE];
    size_t ln;
    char*  file;
    size_t fcap, fn;
} kz_rep;

static void r_init(kz_rep* r, const char* prefix, char* file, size_t cap)
{
    r->prefix = prefix; r->ln = 0; r->file = file; r->fcap = cap; r->fn = 0;
}

static void r_s(kz_rep* r, const char* s)
{
    while (s && *s && r->ln < sizeof r->line - 1) {
        char c = *s++;
        r->line[r->ln++] = (c == '\n' || c == '\r') ? ' ' : c;
    }
}

static void r_hexc(kz_rep* r, uint64_t v, int digits, const char* hx)
{
    char b[17]; int n = 0;
    do { b[n++] = hx[v & 15]; v >>= 4; } while (v && n < 16);
    while (n < digits && n < 16) b[n++] = '0';
    r_s(r, "0x");
    char o[17]; for (int i = 0; i < n; i++) o[i] = b[n - 1 - i];
    o[n] = 0;
    r_s(r, o);
}

static void r_hex(kz_rep* r, uint64_t v, int digits) { r_hexc(r, v, digits, "0123456789abcdef"); }
static void r_code(kz_rep* r, uint32_t v) { r_hexc(r, v, 8, "0123456789ABCDEF"); }   /* 0xC0000005, as Windows writes it */

static void r_dec(kz_rep* r, uint64_t v)
{
    char b[21]; int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char o[21]; for (int i = 0; i < n; i++) o[i] = b[n - 1 - i];
    o[n] = 0;
    r_s(r, o);
}

static void r_secs(kz_rep* r, uint64_t us)   /* "12.3" */
{
    r_dec(r, us / 1000000u);
    char t[3] = { '.', (char)('0' + (us / 100000u) % 10), 0 };
    r_s(r, t);
}

static void r_end(kz_rep* r)
{
    char out[KZ_LINE + 32];
    size_t p = strlen(r->prefix);
    memcpy(out, r->prefix, p);
    memcpy(out + p, r->line, r->ln);
    out[p + r->ln] = '\n';
    raw_write(out, p + r->ln + 1);
    if (r->file && r->fn + r->ln + 1 < r->fcap) {
        memcpy(r->file + r->fn, r->line, r->ln);
        r->fn += r->ln;
        r->file[r->fn++] = '\n';
    }
    r->ln = 0;
}

static void r_line(kz_rep* r, const char* s) { r_s(r, s); r_end(r); }

/* ------------------------------------------------------------------------------------------------------------------
 * The copy in KZ_CRASH_DIR: <kind>-YYYYmmdd-HHMMSS.txt (local time), -2, -3... when that name is taken
 * ----------------------------------------------------------------------------------------------------------------*/
static void stamp(char out[16])   /* YYYYmmdd-HHMMSS */
{
    int Y, M, D, h, m, s;
#ifdef _WIN32
    SYSTEMTIME t;
    GetLocalTime(&t);
    Y = t.wYear; M = t.wMonth; D = t.wDay; h = t.wHour; m = t.wMinute; s = t.wSecond;
#else
    /* time() and the offset taken at install: localtime_r takes the time zone lock, which a crash may hold */
    long long e = (long long)time(NULL) + s_gmtoff;
    long long days = e / 86400; long long rem = e % 86400;
    if (rem < 0) { rem += 86400; days--; }
    h = (int)(rem / 3600); m = (int)(rem % 3600 / 60); s = (int)(rem % 60);
    /* days since 1970-01-01 to a civil date (the proleptic Gregorian calendar, in 400-year eras from 0000-03-01) */
    long long z = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    D = (int)(doy - (153 * mp + 2) / 5 + 1);
    M = (int)(mp < 10 ? mp + 3 : mp - 9);
    Y = (int)(yoe + era * 400 + (M <= 2));
#endif
    int v[6] = { Y, M, D, h, m, s }, w[6] = { 4, 2, 2, 2, 2, 2 }, p = 0;
    for (int i = 0; i < 6; i++) {
        if (i == 3) out[p++] = '-';
        for (int k = w[i] - 1, x = v[i]; k >= 0; k--, x /= 10) out[p + k] = (char)('0' + x % 10);
        p += w[i];
    }
    out[p] = 0;
}

static void write_copy(const char* kind, const char* data, size_t n)
{
    if (!s_dir[0] || !n) return;
    char st[16];
    stamp(st);
#ifdef _WIN32
    CreateDirectoryW(s_dir, NULL);
    size_t dl = wcslen(s_dir);
    for (int k = 1; k < 10; k++) {
        WCHAR path[1100];
        size_t p = 0;
        memcpy(path, s_dir, dl * sizeof(WCHAR)); p = dl;
        path[p++] = L'\\';
        for (const char* c = kind; *c; c++) path[p++] = (WCHAR)*c;
        path[p++] = L'-';
        for (const char* c = st; *c; c++) path[p++] = (WCHAR)*c;
        if (k > 1) { path[p++] = L'-'; path[p++] = (WCHAR)(L'0' + k); }
        memcpy(path + p, L".txt", 5 * sizeof(WCHAR));
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) continue;
            return;
        }
        DWORD w;
        WriteFile(f, data, (DWORD)n, &w, NULL);
        CloseHandle(f);
        return;
    }
#else
    mkdir(s_dir, 0777);
    for (int k = 1; k < 10; k++) {
        char path[1100];
        int p = 0;
        for (const char* c = s_dir; *c && p < 1024; c++) path[p++] = *c;
        path[p++] = '/';
        for (const char* c = kind; *c; c++) path[p++] = *c;
        path[p++] = '-';
        for (const char* c = st; *c; c++) path[p++] = *c;
        if (k > 1) { path[p++] = '-'; path[p++] = (char)('0' + k); }
        memcpy(path + p, ".txt", 5);
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return;
        }
        const char* q = data;
        size_t left = n;
        while (left) {
            ssize_t w = write(fd, q, left);
            if (w < 0) { if (errno == EINTR) continue; break; }
            q += w; left -= (size_t)w;
        }
        close(fd);
        return;
    }
#endif
}

/* ------------------------------------------------------------------------------------------------------------------
 * Memory that may not be there, modules, thread names
 * ----------------------------------------------------------------------------------------------------------------*/
static int safe_read(uintptr_t a, void* out, size_t n)
{
    if (a < 0x1000) return 0;
#ifdef _WIN32
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)a, out, n, &got) && got == n;
#else
    int e = errno;
    struct iovec l = { out, n }, r = { (void*)a, n };
    ssize_t g = syscall(SYS_process_vm_readv, (long)getpid(), &l, 1L, &r, 1L, 0L);
    if (g == (ssize_t)n) { errno = e; return 1; }
    int ok = 0;
    if (g < 0 && errno != EFAULT && s_probe[1] >= 0) {
        /* process_vm_readv refused (a seccomp filter): write() from the address into a pipe fails with EFAULT
         * instead of faulting */
        if (write(s_probe[1], (const void*)a, n) == (ssize_t)n) ok = read(s_probe[0], out, n) == (ssize_t)n;
    }
    errno = e;
    return ok;
#endif
}

static const char* base_name(const char* p)
{
    const char* b = p;
    for (const char* c = p; *c; c++) if (*c == '/' || *c == '\\') b = c + 1;
    return b;
}

/* The module holding address a and a's offset in it. Not found: "unknown" and the address itself. */
static const char* module_of(uintptr_t a, uintptr_t* off, char* buf, size_t cap)
{
    *off = a;
    if (!a) return "unknown";
#ifdef _WIN32
    PVOID base = NULL;
    if (!RtlPcToFileHeader((PVOID)a, &base) || !base) return "unknown";
    *off = a - (uintptr_t)base;
    if ((HMODULE)base == s_exe_base) return s_exe_name;
    /* the name in the module's own export directory (every system and driver DLL has one): read from the loaded
     * image, no loader lock */
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const char*)base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY* ed = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (nt->Signature == IMAGE_NT_SIGNATURE && ed->VirtualAddress && ed->Size) {
            const IMAGE_EXPORT_DIRECTORY* ex = (const IMAGE_EXPORT_DIRECTORY*)((const char*)base + ed->VirtualAddress);
            if (ex->Name) {
                const char* nm = (const char*)base + ex->Name;
                size_t i = 0;
                for (; nm[i] && i < cap - 1; i++) buf[i] = nm[i];
                buf[i] = 0;
                if (i) return buf;
            }
        }
    }
    char path[MAX_PATH];
    if (GetModuleFileNameA((HMODULE)base, path, sizeof path)) {
        const char* b = base_name(path);
        size_t i = 0;
        for (; b[i] && i < cap - 1; i++) buf[i] = b[i];
        buf[i] = 0;
        if (i) return buf;
    }
    return "unknown";
#else
    Dl_info info;
    struct link_map* lm = NULL;
    if (!dladdr1((void*)a, &info, (void**)&lm, RTLD_DL_LINKMAP) || !lm) return "unknown";
    *off = a - (uintptr_t)lm->l_addr;   /* the ELF virtual address: what addr2line and nm use */
    if (!lm->l_name || !lm->l_name[0]) return s_exe_name;   /* the main program */
    const char* b = base_name(lm->l_name);
    size_t i = 0;
    for (; b[i] && i < cap - 1; i++) buf[i] = b[i];
    buf[i] = 0;
    (void)info;
    return i ? buf : "unknown";
#endif
}

static void thread_name(unsigned long tid, char* out, size_t cap)
{
    out[0] = 0;
#ifdef _WIN32
    typedef HRESULT (WINAPI *get_desc_fn)(HANDLE, PWSTR*);
    static get_desc_fn get_desc;
    static int looked;
    if (!looked) {
        HMODULE k = GetModuleHandleA("kernelbase.dll");
        if (k) get_desc = (get_desc_fn)(void*)GetProcAddress(k, "GetThreadDescription");
        if (!get_desc && (k = GetModuleHandleA("kernel32.dll")))
            get_desc = (get_desc_fn)(void*)GetProcAddress(k, "GetThreadDescription");
        looked = 1;
    }
    if (get_desc) {
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)tid);
        if (h) {
            PWSTR w = NULL;
            if (SUCCEEDED(get_desc(h, &w)) && w) {
                WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
                LocalFree(w);
            }
            CloseHandle(h);
        }
    }
#else
    char path[64] = "/proc/self/task/";
    char d[24]; int n = 0;
    unsigned long v = tid;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    size_t p = strlen(path);
    while (n) path[p++] = d[--n];
    memcpy(path + p, "/comm", 6);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t g = read(fd, out, cap - 1);
        close(fd);
        if (g > 0) { out[g] = 0; char* nl = strchr(out, '\n'); if (nl) *nl = 0; }
    }
#endif
    out[cap - 1] = 0;
    for (char* c = out; *c; c++) if ((unsigned char)*c < 0x20) *c = ' ';   /* the rest of the line */
    /* the main thread: its Linux name is the program's, and Windows gives it none */
    if (tid == s_main_tid || !out[0]) snprintf(out, cap, "%s", tid == s_main_tid ? "main" : "unnamed");
}

/* ------------------------------------------------------------------------------------------------------------------
 * Unwinding
 * ----------------------------------------------------------------------------------------------------------------*/
#if defined(_WIN32) && defined(__x86_64__)
/* From c, which is changed. Rsp must stay inside [lo, hi): the stack (or its copy). For a copy of another thread's
 * stack, delta moves a frame pointer from the original range [olo, ohi) into the copy. */
static int unwind_win(CONTEXT* c, uintptr_t* out, int max, uintptr_t lo, uintptr_t hi,
                      uintptr_t olo, uintptr_t ohi, intptr_t delta)
{
    int n = 0;
    while (n < max) {
        const uintptr_t ip = (uintptr_t)c->Rip, sp = (uintptr_t)c->Rsp;
        if (!ip) break;
        out[n++] = ip;
        if (sp < lo || sp + 8 > hi) break;
        DWORD64 base = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c->Rip, &base, NULL);
        if (f) {
            PVOID hd = NULL;
            DWORD64 est = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c->Rip, f, c, &hd, &est, NULL);
        } else {
            DWORD64 ra;   /* a leaf (or a bad PC): the return address is at the stack pointer */
            if (!safe_read(sp, &ra, sizeof ra)) break;
            c->Rip = ra;
            c->Rsp = sp + 8;
        }
        if (delta && c->Rbp >= olo && c->Rbp < ohi) c->Rbp += (DWORD64)delta;
        if ((uintptr_t)c->Rsp <= sp || (uintptr_t)c->Rsp > hi) break;   /* no progress, or off the stack */
    }
    return n;
}

static void stack_bounds(uintptr_t* lo, uintptr_t* hi)
{
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    *lo = (uintptr_t)tib->StackLimit;
    *hi = (uintptr_t)tib->StackBase;
}
#endif

#ifndef _WIN32
typedef struct {
    uintptr_t* out;
    int n, max, started;
    uintptr_t expect;          /* the interrupted PC; 0 = start at the first signal frame */
} kz_uw;

static _Unwind_Reason_Code uw_cb(struct _Unwind_Context* c, void* arg)
{
    kz_uw* u = (kz_uw*)arg;
    int before = 0;
    const uintptr_t ip = (uintptr_t)_Unwind_GetIPInfo(c, &before);
    if (!u->started) {
        if (u->expect ? ip != u->expect : !before) return _URC_NO_REASON;
        u->started = 1;
    }
    if (!ip) return _URC_END_OF_STACK;
    u->out[u->n++] = ip;
    return u->n >= u->max ? _URC_END_OF_STACK : _URC_NO_REASON;
}

/* _Unwind_Backtrace with the fault signals unblocked and a way back: a fault inside it ends the walk, not the
 * process. */
static int unwind_here(uintptr_t* out, int max, uintptr_t expect)
{
    kz_uw u = { out, 0, max, 0, expect };
    sigset_t un, old;
    sigemptyset(&un);
    sigaddset(&un, SIGSEGV); sigaddset(&un, SIGBUS); sigaddset(&un, SIGILL); sigaddset(&un, SIGFPE);
    pthread_sigmask(SIG_SETMASK, NULL, &old);
    if (sigsetjmp(t_recover, 1) == 0) {
        t_recover_armed = 1;
        pthread_sigmask(SIG_UNBLOCK, &un, NULL);
        _Unwind_Backtrace(uw_cb, &u);
    }
    t_recover_armed = 0;
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    return u.n;
}

/* No unwind data to start from (a PC outside any module): the return address at the stack pointer, then the frame
 * pointer chain, every read checked. */
static int unwind_fp(uintptr_t pc, uintptr_t sp, uintptr_t fp, uintptr_t* out, int max)
{
    int n = 0;
    unsigned char b;
    out[n++] = pc;
    if (!safe_read(pc, &b, 1)) {
        uintptr_t ra;
        if (safe_read(sp, &ra, sizeof ra) && safe_read(ra, &b, 1)) out[n++] = ra;
    }
    while (n < max && fp && !(fp & 7)) {
        uintptr_t next, ra;
        if (!safe_read(fp, &next, sizeof next) || !safe_read(fp + 8, &ra, sizeof ra)) break;
        if (!ra || !safe_read(ra, &b, 1)) break;
        out[n++] = ra;
        if (next <= fp || next - fp > (1u << 20)) break;
        fp = next;
    }
    return n;
}
#endif

/* ------------------------------------------------------------------------------------------------------------------
 * The crash report
 * ----------------------------------------------------------------------------------------------------------------*/
static const struct { uint32_t code; const char* name; } kCodes[] = {
    { 0xC0000005u, "EXCEPTION_ACCESS_VIOLATION" },      { 0xC0000006u, "EXCEPTION_IN_PAGE_ERROR" },
    { 0xC000001Du, "EXCEPTION_ILLEGAL_INSTRUCTION" },   { 0xC0000096u, "EXCEPTION_PRIV_INSTRUCTION" },
    { 0x80000002u, "EXCEPTION_DATATYPE_MISALIGNMENT" }, { 0x80000003u, "EXCEPTION_BREAKPOINT" },
    { 0x80000004u, "EXCEPTION_SINGLE_STEP" },           { 0xC000008Cu, "EXCEPTION_ARRAY_BOUNDS_EXCEEDED" },
    { 0xC000008Du, "EXCEPTION_FLT_DENORMAL_OPERAND" },  { 0xC000008Eu, "EXCEPTION_FLT_DIVIDE_BY_ZERO" },
    { 0xC000008Fu, "EXCEPTION_FLT_INEXACT_RESULT" },    { 0xC0000090u, "EXCEPTION_FLT_INVALID_OPERATION" },
    { 0xC0000091u, "EXCEPTION_FLT_OVERFLOW" },          { 0xC0000092u, "EXCEPTION_FLT_STACK_CHECK" },
    { 0xC0000093u, "EXCEPTION_FLT_UNDERFLOW" },         { 0xC0000094u, "EXCEPTION_INT_DIVIDE_BY_ZERO" },
    { 0xC0000095u, "EXCEPTION_INT_OVERFLOW" },          { 0xC00000FDu, "EXCEPTION_STACK_OVERFLOW" },
    { 0xC0000409u, "STATUS_STACK_BUFFER_OVERRUN" },     { 0xC0000374u, "STATUS_HEAP_CORRUPTION" },
    { 0xC0000420u, "STATUS_ASSERTION_FAILURE" },        { 0xE06D7363u, "CPP_EXCEPTION" },
};

static const char* code_name(uint32_t code)
{
    for (size_t i = 0; i < sizeof kCodes / sizeof *kCodes; i++) if (kCodes[i].code == code) return kCodes[i].name;
    return "UNKNOWN_EXCEPTION";
}

#ifndef _WIN32
/* The signal behind win32_compat's exception code (its dispatcher takes SIGSEGV, SIGBUS, SIGILL and SIGFPE). */
static int signal_of(uint32_t code, const char** name)
{
    switch (code) {
    case 0xC0000005u:                     *name = "SIGSEGV"; return SIGSEGV;
    case 0xC0000006u: case 0x80000002u:   *name = "SIGBUS";  return SIGBUS;
    case 0xC000001Du: case 0xC0000096u:   *name = "SIGILL";  return SIGILL;
    case 0x80000003u: case 0x80000004u:   *name = "SIGTRAP"; return SIGTRAP;
    default:                              *name = "SIGFPE";  return SIGFPE;
    }
}
#endif

typedef struct {
    int         is_signal;      /* a signal line (Linux, abort) rather than an exception line */
    int         sig;
    const char* sig_name;
    uint32_t    code;
    int         access;         /* -1 unknown, 0 read, 1 write, 8 execute */
    uintptr_t   fault_addr;
    const CONTEXT* ctx;         /* the registers; NULL for abort */
    uintptr_t   frames[KZ_MAX_FRAMES];
    int         nframes;
    unsigned long tid;          /* the thread that faulted (the report may be written from another) */
} kz_crash;

static char s_crash_file[96 * 1024];
static char s_render[4096];

static uint32_t fnv(uint32_t h, const char* s)
{
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static void r_frame(kz_rep* r, uintptr_t a, uint32_t* hash)
{
    char mb[128];
    uintptr_t off;
    const char* m = module_of(a, &off, mb, sizeof mb);
    size_t start = r->ln;
    r_s(r, m); r_s(r, "+"); r_hex(r, off, 0);
    if (hash) {
        char t[KZ_LINE];
        size_t len = r->ln - start;
        memcpy(t, r->line + start, len); t[len] = 0;
        *hash = fnv(fnv(*hash, "|"), t);
    }
}

#if defined(__x86_64__)
static void r_reg(kz_rep* r, const char* name, uint64_t v) { r_s(r, " "); r_s(r, name); r_s(r, "="); r_hex(r, v, 16); }
#endif

static void report_crash(kz_crash* c)
{
    ps3_stdio_flush_crash();   /* what the run printed before this (at once if nothing is buffered) */
    kz_rep r;
    r_init(&r, "[kz-crash] ", s_crash_file, sizeof s_crash_file);
    r_line(&r, "begin");
    uint32_t hash = 2166136261u;
    char tmp[24];
    if (c->is_signal) {
        r_s(&r, "signal: "); r_dec(&r, (uint64_t)c->sig); r_s(&r, " "); r_s(&r, c->sig_name); r_end(&r);
        tmp[0] = 0; { kz_rep t; r_init(&t, "", NULL, 0); r_dec(&t, (uint64_t)c->sig); memcpy(tmp, t.line, t.ln); tmp[t.ln] = 0; }
        hash = fnv(fnv(hash, "signal "), tmp);
    } else {
        r_s(&r, "exception: "); r_code(&r, c->code); r_s(&r, " "); r_s(&r, code_name(c->code)); r_end(&r);
        { kz_rep t; r_init(&t, "", NULL, 0); r_code(&t, c->code); memcpy(tmp, t.line, t.ln); tmp[t.ln] = 0; }
        hash = fnv(fnv(hash, "exception "), tmp);
    }
    const uintptr_t pc = c->nframes ? c->frames[0] : 0;
    r_s(&r, "address: "); r_hex(&r, pc, 16); r_end(&r);
    if (c->access >= 0) {
        r_s(&r, "access: ");
        r_s(&r, c->access == 8 ? "execute " : c->access == 1 ? "write " : "read ");
        r_hex(&r, c->fault_addr, 16); r_end(&r);
    }
    r_s(&r, "module: "); r_frame(&r, pc, NULL); r_end(&r);
    {
        char nm[64];
        unsigned long tid = c->tid ? c->tid : cur_tid();
        thread_name(tid, nm, sizeof nm);
        r_s(&r, "thread: "); r_dec(&r, tid); r_s(&r, " "); r_s(&r, nm); r_end(&r);
    }
    r_s(&r, "uptime: "); r_secs(&r, now_us() - s_start_us); r_s(&r, " s"); r_end(&r);
    if (s_version[0]) { r_s(&r, "version: "); r_s(&r, s_version); r_end(&r); }
#if defined(__x86_64__)
    if (c->ctx) {
        const CONTEXT* x = c->ctx;
        r_s(&r, "registers:"); r_reg(&r, "rip", x->Rip); r_reg(&r, "rsp", x->Rsp); r_reg(&r, "rbp", x->Rbp);
        r_reg(&r, "eflags", x->EFlags); r_end(&r);
        r_s(&r, "registers:"); r_reg(&r, "rax", x->Rax); r_reg(&r, "rbx", x->Rbx); r_reg(&r, "rcx", x->Rcx);
        r_reg(&r, "rdx", x->Rdx); r_end(&r);
        r_s(&r, "registers:"); r_reg(&r, "rsi", x->Rsi); r_reg(&r, "rdi", x->Rdi); r_reg(&r, "r8", x->R8);
        r_reg(&r, "r9", x->R9); r_end(&r);
        r_s(&r, "registers:"); r_reg(&r, "r10", x->R10); r_reg(&r, "r11", x->R11); r_reg(&r, "r12", x->R12);
        r_reg(&r, "r13", x->R13); r_end(&r);
        r_s(&r, "registers:"); r_reg(&r, "r14", x->R14); r_reg(&r, "r15", x->R15); r_end(&r);
    }
#endif
    for (int i = 0; i < c->nframes; i++) {
        r_s(&r, "#"); r_dec(&r, (uint64_t)i); r_s(&r, " ");
        r_frame(&r, c->frames[i], i < KZ_SIG_FRAMES ? &hash : NULL);
        r_end(&r);
    }
    {
        static const char hx[] = "0123456789ABCDEF";
        char sig[16] = "KZ-XXXX-XXXX";
        for (int i = 0; i < 8; i++) sig[3 + i + (i >= 4)] = hx[(hash >> (28 - 4 * i)) & 15];
        r_s(&r, "signature: "); r_s(&r, sig); r_end(&r);
    }
    if (rsx_vulkan_breadcrumbs) {   /* last: the lines above are out even if this does not return */
        s_render[0] = 0;
        if (rsx_vulkan_breadcrumbs(s_render, sizeof s_render) > 0) {
            s_render[sizeof s_render - 1] = 0;
            for (char* l = s_render; *l;) {
                char* e = strchr(l, '\n');
                if (e) *e = 0;
                if (*l) { r_s(&r, "render: "); r_s(&r, l); r_end(&r); }
                if (!e) break;
                l = e + 1;
            }
        }
    }
    r_line(&r, "end");
    write_copy("crash", r.file, r.fn);
}

/* One report at a time: a second thread waits for the first (whose end ends the process), a fault inside the report
 * ends it as it would have. Returns 1 when this thread reports. */
static int crash_enter(void)
{
    const unsigned long me = cur_tid();
    unsigned long none = 0;
    if (__atomic_compare_exchange_n(&s_crashing, &none, me, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 1;
    if (none == me) return 0;
    for (int i = 0; i < 1000 && !__atomic_load_n(&s_crash_done, __ATOMIC_ACQUIRE); i++) Sleep(10);
    Sleep(2000);   /* the reporting thread is ending the process */
    return 0;
}

typedef struct {
    EXCEPTION_POINTERS* ep;
    unsigned long tid;
    uintptr_t lo, hi;   /* the faulting thread's stack (Windows) */
} kz_fault;
static void crash_collect(kz_fault* f);
#ifdef _WIN32
static DWORD WINAPI kz_crash_report_thread(LPVOID p);
#endif

static LONG WINAPI kz_crash_filter(EXCEPTION_POINTERS* ep)
{
#ifndef _WIN32
    if (t_recover_armed) { t_recover_armed = 0; siglongjmp(t_recover, 1); }   /* a fault inside our own unwind */
#endif
    if (!crash_enter()) return EXCEPTION_CONTINUE_SEARCH;
#if defined(_WIN32)
    {
        uintptr_t lo = 0, hi = 0;
#if defined(__x86_64__)
        stack_bounds(&lo, &hi);
#endif
        kz_fault f = { ep, cur_tid(), lo, hi };
        HANDLE h = NULL;
        /* Out of stack: the little that is left (the 64 KB guarantee, when the system keeps it) may be too little for the
         * report, so a new thread writes it and this one waits. */
        if ((uint32_t)ep->ExceptionRecord->ExceptionCode == 0xC00000FDu)
            h = CreateThread(NULL, 256 * 1024, kz_crash_report_thread, &f, 0, NULL);
        if (h) {
            WaitForSingleObject(h, 30000);
            CloseHandle(h);
        } else {
            crash_collect(&f);
        }
    }
#else
    kz_fault f = { ep, cur_tid(), 0, 0 };
    crash_collect(&f);
#endif
    __atomic_store_n(&s_crash_done, 1, __ATOMIC_RELEASE);
    LONG r = s_prev_filter ? s_prev_filter(ep) : EXCEPTION_CONTINUE_SEARCH;
    if (r == EXCEPTION_CONTINUE_EXECUTION) return r;
#ifdef _WIN32
    if (!s_wer) TerminateProcess(GetCurrentProcess(), (UINT)ep->ExceptionRecord->ExceptionCode);
#endif
    return r;
}

/* What the filter reports: the exception, its thread and (Windows) that thread's stack. */
static void crash_collect(kz_fault* f)
{
    EXCEPTION_POINTERS* ep = f->ep;
    static kz_crash c;
    memset(&c, 0, sizeof c);
    c.tid = f->tid;
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    c.code = (uint32_t)er->ExceptionCode;
    c.ctx = ep->ContextRecord;
    c.access = -1;
#ifndef _WIN32
    c.is_signal = 1;
    c.sig = signal_of(c.code, &c.sig_name);
#endif
    const uintptr_t pc = (uintptr_t)er->ExceptionAddress;
    if ((c.code == 0xC0000005u || c.code == 0xC0000006u) && er->NumberParameters >= 2) {
        c.fault_addr = (uintptr_t)er->ExceptionInformation[1];
        c.access = er->ExceptionInformation[0] == 8 ? 8 : er->ExceptionInformation[0] == 1 ? 1 : 0;
        if (c.access == 0 && c.fault_addr == pc) c.access = 8;   /* a jump or call to where nothing runs */
    }
#if defined(_WIN32) && defined(__x86_64__)
    {
        CONTEXT copy = *ep->ContextRecord;
        c.nframes = unwind_win(&copy, c.frames, KZ_MAX_FRAMES, f->lo, f->hi, 0, 0, 0);
    }
#elif defined(_WIN32)
    c.frames[0] = pc; c.nframes = 1;
#else
    {
        unsigned char b;
        if (safe_read(pc, &b, 1)) c.nframes = unwind_here(c.frames, KZ_MAX_FRAMES, pc);
        if (!c.nframes) c.nframes = unwind_fp(pc, (uintptr_t)ep->ContextRecord->Sp, (uintptr_t)ep->ContextRecord->Fp,
                                              c.frames, KZ_MAX_FRAMES);
    }
#endif
    if (!c.nframes) { c.frames[0] = pc; c.nframes = 1; }
    report_crash(&c);
}

#ifdef _WIN32
static DWORD WINAPI kz_crash_report_thread(LPVOID p)
{
    crash_collect((kz_fault*)p);
    return 0;
}
#endif

/* abort(), std::terminate and failed asserts. ps3_stdio_init's handler (installed with signal() later) calls this one
 * after its flush; returning lets abort() end the process as it would have. */
#ifdef _WIN32
static void kz_on_abort(int sig)
{
    if (crash_enter()) {
        static kz_crash c;
        memset(&c, 0, sizeof c);
        c.is_signal = 1; c.sig = SIGABRT; c.sig_name = "SIGABRT"; c.access = -1;
#if defined(__x86_64__)
        CONTEXT ctx;
        RtlCaptureContext(&ctx);
        uintptr_t lo, hi, tmp[KZ_MAX_FRAMES + 4];
        stack_bounds(&lo, &hi);
        int n = unwind_win(&ctx, tmp, KZ_MAX_FRAMES + 4, lo, hi, 0, 0, 0);
        /* drop this handler's own frame */
        DWORD64 base = 0;
        PRUNTIME_FUNCTION self = RtlLookupFunctionEntry((DWORD64)(uintptr_t)&kz_on_abort, &base, NULL);
        int skip = 0;
        while (skip < n && self) {
            DWORD64 b2 = 0;
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(tmp[skip], &b2, NULL);
            if (f != self) break;
            skip++;
        }
        for (int i = skip; i < n && c.nframes < KZ_MAX_FRAMES; i++) c.frames[c.nframes++] = tmp[i];
#endif
        report_crash(&c);
        __atomic_store_n(&s_crash_done, 1, __ATOMIC_RELEASE);
    }
    if (s_prev_abort && s_prev_abort != SIG_DFL && s_prev_abort != SIG_IGN && s_prev_abort != SIG_ERR) s_prev_abort(sig);
}
#else
static struct sigaction s_prev_abort;
static void kz_on_abort(int sig)
{
    const int e = errno;
    if (crash_enter()) {
        static kz_crash c;
        memset(&c, 0, sizeof c);
        c.is_signal = 1; c.sig = SIGABRT; c.sig_name = "SIGABRT"; c.access = -1;
        c.nframes = unwind_here(c.frames, KZ_MAX_FRAMES, 0);
        report_crash(&c);
        __atomic_store_n(&s_crash_done, 1, __ATOMIC_RELEASE);
    }
    if (!(s_prev_abort.sa_flags & SA_SIGINFO) && s_prev_abort.sa_handler != SIG_DFL &&
        s_prev_abort.sa_handler != SIG_IGN && s_prev_abort.sa_handler)
        s_prev_abort.sa_handler(sig);
    errno = e;
}
#endif

void kz_crash_install(const char* version_line)
{
    static int done;
    if (done) return;
    done = 1;
    s_start_us = now_us();
    s_main_tid = cur_tid();
    if (version_line) strncpy(s_version, version_line, sizeof s_version - 1);
#ifdef _WIN32
    {
        intptr_t h = _get_osfhandle(_fileno(stderr));
        s_err_h = (h != -1 && h != -2) ? (HANDLE)h : GetStdHandle(STD_ERROR_HANDLE);
        s_err_disk = s_err_h && s_err_h != INVALID_HANDLE_VALUE && GetFileType(s_err_h) == FILE_TYPE_DISK;
    }
    s_exe_base = GetModuleHandleW(NULL);
    {
        WCHAR p[MAX_PATH];
        DWORD n = GetModuleFileNameW(NULL, p, MAX_PATH);
        if (n && n < MAX_PATH) {
            const WCHAR* b = p;
            for (const WCHAR* c = p; *c; c++) if (*c == L'\\' || *c == L'/') b = c + 1;
            if (!WideCharToMultiByte(CP_UTF8, 0, b, -1, s_exe_name, sizeof s_exe_name, NULL, NULL))
                strcpy(s_exe_name, "KZ1HD.exe");
        }
    }
    /* KZ_CRASH_DIR=<folder> (default unset: no copy): where crash-*.txt and hang-*.txt go; the launcher sets
     * <work>/crash. Read as UTF-16 so a folder with any name works. */
    {
        const WCHAR* d = _wgetenv(L"KZ_CRASH_DIR");
        if (d && *d && wcslen(d) < 1000) wcscpy(s_dir, d);
    }
    /* KZ_CRASH_WER=1 (default off): after the report, hand the crash to Windows (Windows Error Reporting, a debugger
     * set as AeDebug) instead of ending the process with the exception code */
    { const char* w = getenv("KZ_CRASH_WER"); s_wer = w && w[0] == '1'; }
    /* the report of a stack overflow runs on what is left of the stack: keep 64 KB for it (main thread) */
    { ULONG g = 64 * 1024; SetThreadStackGuarantee(&g); }
    void (*prev)(int) = signal(SIGABRT, kz_on_abort);
    s_prev_abort = prev == SIG_ERR ? NULL : prev;
#else
    {
        char p[1024];
        ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
        if (n > 0) {
            p[n] = 0;
            const char* b = base_name(p);
            size_t i = 0;
            for (; b[i] && i < sizeof s_exe_name - 1; i++) s_exe_name[i] = b[i];
            s_exe_name[i] = 0;
        }
    }
    /* KZ_CRASH_DIR=<folder> (default unset: no copy): where crash-*.txt and hang-*.txt go; the launcher sets
     * <work>/crash */
    {
        const char* d = getenv("KZ_CRASH_DIR");
        if (d && *d && strlen(d) < 1000) strcpy(s_dir, d);
    }
    { time_t t = time(NULL); struct tm tm; if (localtime_r(&t, &tm)) s_gmtoff = tm.tm_gmtoff; }
    if (pipe(s_probe) == 0) {
        fcntl(s_probe[0], F_SETFL, O_NONBLOCK); fcntl(s_probe[1], F_SETFL, O_NONBLOCK);
        fcntl(s_probe[0], F_SETFD, FD_CLOEXEC); fcntl(s_probe[1], F_SETFD, FD_CLOEXEC);
    }
    /* an alternate signal stack for this thread, so a stack overflow on it still runs the dispatcher (installed
     * SA_ONSTACK) and this report */
    {
        static char alt[128 * 1024] __attribute__((aligned(16)));
        stack_t ss;
        memset(&ss, 0, sizeof ss);
        ss.ss_sp = alt; ss.ss_size = sizeof alt;
        sigaltstack(&ss, NULL);
    }
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = kz_on_abort;
        sa.sa_flags = SA_ONSTACK;   /* a plain handler: ps3_stdio's signal() calls it with the signal number only */
        sigemptyset(&sa.sa_mask);
        sigaction(SIGABRT, &sa, &s_prev_abort);
    }
#endif
    s_prev_filter = SetUnhandledExceptionFilter(kz_crash_filter);
}

/* ------------------------------------------------------------------------------------------------------------------
 * The hang watchdog
 * ----------------------------------------------------------------------------------------------------------------*/
static unsigned (*s_frames_fn)(void);
static volatile int s_minimized;
static volatile uint64_t s_last_tick_us;
static unsigned s_hang_secs = 5;
static char s_hang_file[256 * 1024];

void kz_hang_tick(int minimized)
{
    s_minimized = minimized;
    s_last_tick_us = now_us();
}

static void r_frames(kz_rep* r, const uintptr_t* f, int n)
{
    for (int i = 0; i < n; i++) { r_s(r, "#"); r_dec(r, (uint64_t)i); r_s(r, " "); r_frame(r, f[i], NULL); r_end(r); }
}

#ifndef _WIN32
#define KZ_HANG_SIG (SIGRTMIN + 6)   /* SIGRTMIN+4 is win32_compat's SuspendThread */
static volatile long s_hg_target;   /* the thread asked for its stack */
static volatile int  s_hg_n;
static uintptr_t     s_hg_frames[KZ_MAX_FRAMES];

static void hang_sig(int sig, siginfo_t* si, void* uctx)
{
    (void)sig; (void)si;
    const int e = errno;
    const long me = (long)syscall(SYS_gettid);
    if (__atomic_load_n(&s_hg_target, __ATOMIC_ACQUIRE) != me) { errno = e; return; }
    uintptr_t f[KZ_MAX_FRAMES];
    uintptr_t pc = 0, sp = 0, fp = 0;
    const ucontext_t* uc = (const ucontext_t*)uctx;
#if defined(__x86_64__)
    pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP]; sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
#elif defined(__aarch64__)
    pc = (uintptr_t)uc->uc_mcontext.pc; sp = (uintptr_t)uc->uc_mcontext.sp; fp = (uintptr_t)uc->uc_mcontext.regs[29];
#endif
    int n = unwind_here(f, KZ_MAX_FRAMES, pc);
    if (!n && pc) n = unwind_fp(pc, sp, fp, f, KZ_MAX_FRAMES);
    if (__atomic_load_n(&s_hg_target, __ATOMIC_ACQUIRE) == me) {
        memcpy(s_hg_frames, f, sizeof(uintptr_t) * (size_t)n);
        __atomic_store_n(&s_hg_n, n, __ATOMIC_RELEASE);
    }
    errno = e;
}

static void dump_threads(kz_rep* r)
{
    static long tids[1024];
    int nt = 0;
    DIR* d = opendir("/proc/self/task");
    if (!d) { r_line(r, "threads: /proc/self/task cannot be read"); return; }
    for (struct dirent* e; (e = readdir(d)) && nt < 1024;) {
        long t = atol(e->d_name);
        if (t > 0) tids[nt++] = t;
    }
    closedir(d);
    const pid_t pid = getpid();
    for (int i = 0; i < nt; i++) {
        if ((unsigned long)tids[i] == s_watch_tid) continue;
        char nm[64];
        thread_name((unsigned long)tids[i], nm, sizeof nm);
        __atomic_store_n(&s_hg_n, -1, __ATOMIC_RELEASE);
        __atomic_store_n(&s_hg_target, tids[i], __ATOMIC_RELEASE);
        int n = -1;
        if (syscall(SYS_tgkill, (long)pid, tids[i], KZ_HANG_SIG) == 0) {
            for (int w = 0; w < 500 && (n = __atomic_load_n(&s_hg_n, __ATOMIC_ACQUIRE)) < 0; w++) Sleep(1);
        }
        __atomic_store_n(&s_hg_target, 0, __ATOMIC_RELEASE);
        Sleep(1);   /* a late handler sees the target gone */
        r_s(r, "thread: "); r_dec(r, (uint64_t)tids[i]); r_s(r, " "); r_s(r, nm); r_end(r);
        if (n < 0) r_line(r, "frames: not available (the thread did not answer)");
        else r_frames(r, s_hg_frames, n);
    }
}
#elif defined(__x86_64__)
typedef LONG (NTAPI *nt_qit_fn)(HANDLE, int, PVOID, ULONG, PULONG);
typedef struct { LONG ExitStatus; PVOID TebBaseAddress; PVOID UniqueProcess, UniqueThread; ULONG_PTR AffinityMask;
                 LONG Priority, BasePriority; } kz_tbi;
static unsigned char s_stack_copy[512 * 1024];

static int thread_frames(DWORD id, uintptr_t* out)
{
    static nt_qit_fn qit;
    if (!qit) qit = (nt_qit_fn)(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
    if (!h) return -1;
    int n = -1;
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_FULL;
    uintptr_t sp = 0;
    SIZE_T got = 0;
    /* While the thread is stopped: its registers and a copy of its stack, nothing that takes a lock (no heap, no
     * loader); the unwind runs on the copy after it is let go. */
    if (SuspendThread(h) != (DWORD)-1) {
        if (GetThreadContext(h, &c)) {
            sp = (uintptr_t)c.Rsp;
            uintptr_t top = sp + sizeof s_stack_copy;
            kz_tbi tbi;
            NT_TIB tib;
            if (qit && qit(h, 0, &tbi, sizeof tbi, NULL) >= 0 &&
                ReadProcessMemory(GetCurrentProcess(), tbi.TebBaseAddress, &tib, sizeof tib, NULL) &&
                (uintptr_t)tib.StackBase > sp && (uintptr_t)tib.StackBase - sp < sizeof s_stack_copy)
                top = (uintptr_t)tib.StackBase;
            if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)sp, s_stack_copy, top - sp, &got)) {
                /* past the stack's end: take what is readable, a page at a time */
                got = 0;
                while (sp + got < top) {
                    const uintptr_t at = sp + got;
                    SIZE_T want = 4096 - (at & 4095), g = 0;
                    if (want > top - at) want = top - at;
                    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)at, s_stack_copy + got, want, &g) || !g)
                        break;
                    got += g;
                }
            }
            n = 0;
        }
        ResumeThread(h);
    }
    CloseHandle(h);
    if (n < 0) return -1;
    const intptr_t delta = (intptr_t)(uintptr_t)s_stack_copy - (intptr_t)sp;
    c.Rsp += (DWORD64)delta;
    if (c.Rbp >= sp && c.Rbp < sp + got) c.Rbp += (DWORD64)delta;
    return unwind_win(&c, out, KZ_MAX_FRAMES, (uintptr_t)s_stack_copy, (uintptr_t)s_stack_copy + got, sp, sp + got,
                      delta);
}

static void dump_threads(kz_rep* r)
{
    static DWORD ids[1024];
    int nt = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { r_line(r, "threads: no thread snapshot"); return; }
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    const DWORD pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok && nt < 1024; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid) ids[nt++] = te.th32ThreadID;
    CloseHandle(snap);
    for (int i = 0; i < nt; i++) {
        if (ids[i] == s_watch_tid) continue;
        uintptr_t f[KZ_MAX_FRAMES];
        int n = thread_frames(ids[i], f);
        char nm[64];
        thread_name(ids[i], nm, sizeof nm);
        r_s(r, "thread: "); r_dec(r, ids[i]); r_s(r, " "); r_s(r, nm); r_end(r);
        if (n < 0) r_line(r, "frames: not available (the thread could not be stopped)");
        else r_frames(r, f, n);
    }
}
#else
static void dump_threads(kz_rep* r) { r_line(r, "threads: not supported on this host"); }
#endif

static void hang_report(unsigned secs, unsigned frames)
{
    ps3_stdio_flush_crash();
    kz_rep r;
    r_init(&r, "[kz-hang] ", s_hang_file, sizeof s_hang_file);
    r_line(&r, "begin");
    r_s(&r, "no new frame for "); r_dec(&r, secs); r_s(&r, " s"); r_end(&r);
    if (s_version[0]) { r_s(&r, "version: "); r_s(&r, s_version); r_end(&r); }
    const uint64_t now = now_us();
    r_s(&r, "uptime: "); r_secs(&r, now - s_start_us); r_s(&r, " s"); r_end(&r);
    r_s(&r, "frames: "); r_dec(&r, frames); r_end(&r);
    const uint64_t lt = s_last_tick_us;
    if (lt) { r_s(&r, "frame loop: last ran "); r_secs(&r, now > lt ? now - lt : 0); r_s(&r, " s ago"); r_end(&r); }
    else r_line(&r, "frame loop: no tick seen");
    dump_threads(&r);
    r_line(&r, "end");
    write_copy("hang", r.file, r.fn);
}

static DWORD WINAPI hang_watch(LPVOID arg)
{
    (void)arg;
    ps3_thread_name("kz hang watch");
    s_watch_tid = cur_tid();
    unsigned last = s_frames_fn();
    uint64_t since = now_us();
    int armed = last != 0, level = 0, episodes = 0, reported = 0;
    for (;;) {
        Sleep(200);
        if (__atomic_load_n(&s_crashing, __ATOMIC_ACQUIRE)) continue;
        const unsigned f = s_frames_fn();
        const uint64_t t = now_us();
        if (f != last) {
            if (level) fprintf(stderr, "[kz] hang watch: frames again after %.1f s\n", (double)(t - since) / 1e6);
            last = f; since = t; level = 0; armed = 1;
            continue;
        }
        if (!armed || s_minimized) { since = t; continue; }
        const uint64_t stalled = t - since;
        if (level == 0 && stalled >= (uint64_t)s_hang_secs * 1000000u) {
            level = 1;
            reported = episodes < 3;   /* three hangs a session at most */
            if (reported) { episodes++; hang_report(s_hang_secs, f); }
        } else if (level == 1 && stalled >= (uint64_t)s_hang_secs * 6000000u) {
            level = 2;
            if (reported) hang_report(s_hang_secs * 6, f);
        }
    }
    return 0;
}

void kz_hang_watch_start(unsigned (*frames)(void))
{
    /* KZ_HANG_WATCH=<seconds> (default 5): a hang report after that long without a new frame, another at six times
     * as long; 0 = no watchdog */
    const char* e = getenv("KZ_HANG_WATCH");
    if (e && *e) {
        const long v = atol(e);
        if (v <= 0) { fprintf(stderr, "[kz] hang watch: off (KZ_HANG_WATCH=%s)\n", e); return; }
        s_hang_secs = v > 3600 ? 3600 : (unsigned)v;
    }
    if (!frames || s_frames_fn) return;
    s_frames_fn = frames;
#ifndef _WIN32
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = hang_sig;
    sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(KZ_HANG_SIG, &sa, NULL);
#endif
    HANDLE t = CreateThread(NULL, 256 * 1024, hang_watch, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/* ------------------------------------------------------------------------------------------------------------------
 * KZ_CRASH_TEST
 * ----------------------------------------------------------------------------------------------------------------*/
static const char* s_test;
static unsigned s_test_ms = 5000;
static volatile int s_hang_fire;
static volatile int* volatile s_null;   /* always NULL; volatile so no compiler can prove it */
static volatile int s_never;

__attribute__((noinline)) int kz_crash_null_read(volatile int* p) { return *p; }
__attribute__((noinline)) void kz_crash_null_write(volatile int* p) { *p = 0x4B5A; }

__attribute__((noinline)) static int kz_crash_recurse(volatile char* prev, int depth)
{
    volatile char pad[2048];
    pad[0] = (char)depth;
    pad[2047] = prev ? prev[0] : 0;
    if (s_never) return 0;
    const int r = kz_crash_recurse(pad, depth + 1);
    return r + pad[depth & 2047];
}

static int test_is(const char* k) { return s_test && strcmp(s_test, k) == 0; }

static void test_fire(void)
{
    fprintf(stderr, "[kz] KZ_CRASH_TEST=%s: now\n", s_test);
    fflush(stderr);
    if (test_is("null-main") || test_is("null-thread")) {
        if (test_is("null-main")) (void)kz_crash_null_read(s_null);
        else kz_crash_null_write(s_null);
    } else if (test_is("abort")) {
        abort();
    } else if (test_is("illegal")) {
        __builtin_trap();
    } else if (test_is("stack")) {
        (void)kz_crash_recurse(NULL, 0);
    } else if (test_is("fastfail")) {
#ifdef _WIN32
        __asm__ volatile("int $0x29" : : "c"(7));   /* __fastfail(FAST_FAIL_FATAL_APP_EXIT): no handler runs */
#else
        kill(getpid(), SIGKILL);                     /* the same on Linux: an end nothing in the process sees */
#endif
    }
}

static DWORD WINAPI test_thread(LPVOID arg)
{
    (void)arg;
    ps3_thread_name("kz crash test");
    Sleep(s_test_ms);
    if (test_is("hang")) {
        fprintf(stderr, "[kz] KZ_CRASH_TEST=hang: the frame loop stops now\n");
        s_hang_fire = 1;
    } else {
        test_fire();
    }
    return 0;
}

void kz_crash_test_start(void)
{
    /* KZ_CRASH_TEST=<kind> (default unset), for testing a build's crash reports: null-main, null-thread, abort,
     * illegal, stack, fastfail, hang. KZ_CRASH_TEST_DELAY=<seconds> (default 5) before it fires. */
    static const char* const kinds[] = { "null-main", "null-thread", "abort", "illegal", "stack", "fastfail", "hang" };
    const char* k = getenv("KZ_CRASH_TEST");
    if (!k || !*k) return;
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) if (strcmp(k, kinds[i]) == 0) s_test = kinds[i];
    if (!s_test) {
        fprintf(stderr, "[kz] KZ_CRASH_TEST=%s: not a kind (null-main, null-thread, abort, illegal, stack, fastfail, "
                        "hang); ignored\n", k);
        return;
    }
    const char* d = getenv("KZ_CRASH_TEST_DELAY");
    if (d && *d) { const double v = atof(d); s_test_ms = v > 0 ? (unsigned)(v * 1000.0) : 0; }
    const int on_thread = test_is("null-thread") || test_is("hang");
    fprintf(stderr, "[kz] KZ_CRASH_TEST=%s: in %.1f s, %s\n", s_test, s_test_ms / 1000.0,
            on_thread ? "on a thread of its own" : "on the main thread, before the game's code starts");
    if (on_thread) {
        HANDLE t = CreateThread(NULL, 256 * 1024, test_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
}

void kz_crash_test_main_point(void)
{
    if (!s_test || test_is("null-thread") || test_is("hang")) return;
    Sleep(s_test_ms);
    ps3_stdio_init();   /* as ppu_run would: its filter above this one, the buffered streams */
    test_fire();
}

void kz_crash_test_hang_point(void)
{
    if (!s_hang_fire) return;
    for (;;) Sleep(1000);
}
