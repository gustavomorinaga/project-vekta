/*
 * The session header: "[kz-sys] <key>: <value>" lines (sys_info.h, docs/dev/crash-reports.md). Ordinary code, run
 * once at start (the display lines once the window exists); nothing here runs in a crash.
 *   os          Windows: RtlGetVersion's version and build, and Wine's version when it runs under Wine;
 *               Linux: /etc/os-release's PRETTY_NAME and uname
 *   cpu         the brand string (CPUID 0x80000002-4)
 *   cores       cores and threads
 *   memory      physical memory, total and available
 *   env         the KZ_* and PS3* variables set when the game started (the home folder written ~)
 *   started-by  KZ_STARTED_BY (the launcher sets setup, zip, recomplauncher, quiver or source), else unknown
 *   display     one per monitor: WxH@Hz, and on Windows the scale (the monitor's DPI over 96)
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE   /* RTLD_DEFAULT */
#endif
#include "sys_info.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/utsname.h>
#include <unistd.h>
extern char** environ;
#endif

static void line(const char* key, const char* value) { fprintf(stderr, "[kz-sys] %s: %s\n", key, value); }

void kz_sys_log_line(const char* key, const char* value) { line(key, value && *value ? value : "none"); }

/* ------------------------------------------------------------------------------------------------------------------
 * name=value pairs
 * ----------------------------------------------------------------------------------------------------------------*/
static const char* home_dir(void)
{
#ifdef _WIN32
    const char* h = getenv("USERPROFILE");
#else
    const char* h = getenv("HOME");
#endif
    return h && strlen(h) > 1 ? h : NULL;
}

static void put(char* out, unsigned long cap, size_t* n, const char* s, size_t len)
{
    for (size_t i = 0; i < len && *n + 1 < cap; i++) out[(*n)++] = s[i];
    out[*n] = 0;
}

void kz_sys_pair(char* out, unsigned long cap, const char* name, const char* value)
{
    if (!cap) return;
    size_t n = strnlen(out, cap - 1);
    if (n) put(out, cap, &n, " ", 1);
    put(out, cap, &n, name, strlen(name));
    put(out, cap, &n, "=", 1);
    const char* home = home_dir();
    const char* v = value ? value : "";
    char buf[1024];
    if (home && strncmp(v, home, strlen(home)) == 0) {
        snprintf(buf, sizeof buf, "~%s", v + strlen(home));
        v = buf;
    }
    const int quote = !*v || strpbrk(v, " \t\"") != NULL;
    if (quote) put(out, cap, &n, "\"", 1);
    for (const char* c = v; *c; c++) {
        if (*c == '"' || *c == '\\') put(out, cap, &n, "\\", 1);
        if ((unsigned char)*c >= 0x20) put(out, cap, &n, c, 1);
    }
    if (quote) put(out, cap, &n, "\"", 1);
}

static int env_wanted(const char* e)
{
    return strncmp(e, "KZ_", 3) == 0 || strncmp(e, "PS3", 3) == 0;
}

static int cmp_str(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }

void kz_sys_env_line(char* const* envp, char* out, unsigned long cap)
{
    if (!cap) return;
    out[0] = 0;
    const char* pick[256];
    size_t np = 0;
    for (char* const* e = envp; e && *e && np < 256; e++) if (env_wanted(*e) && strchr(*e, '=')) pick[np++] = *e;
    qsort(pick, np, sizeof *pick, cmp_str);
    for (size_t i = 0; i < np; i++) {
        const char* eq = strchr(pick[i], '=');
        char name[128];
        size_t len = (size_t)(eq - pick[i]);
        if (len >= sizeof name) continue;
        memcpy(name, pick[i], len); name[len] = 0;
        kz_sys_pair(out, cap, name, eq + 1);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 * The facts
 * ----------------------------------------------------------------------------------------------------------------*/
static void cpu_brand(char* out, size_t cap)
{
    snprintf(out, cap, "unknown");
#if defined(__x86_64__) || defined(__i386__)
    unsigned r[12];
    unsigned max = __get_cpuid_max(0x80000000u, NULL);
    if (max < 0x80000004u) return;
    for (unsigned i = 0; i < 3; i++) __get_cpuid(0x80000002u + i, &r[4 * i], &r[4 * i + 1], &r[4 * i + 2], &r[4 * i + 3]);
    char b[49];
    memcpy(b, r, 48); b[48] = 0;
    const char* s = b;
    while (*s == ' ') s++;
    snprintf(out, cap, "%s", s);
    for (size_t n = strlen(out); n && out[n - 1] == ' '; n--) out[n - 1] = 0;
#endif
}

#ifdef _WIN32
static void os_line(char* out, size_t cap)
{
    typedef LONG (WINAPI *rtl_ver_fn)(OSVERSIONINFOW*);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    rtl_ver_fn ver = nt ? (rtl_ver_fn)(void*)GetProcAddress(nt, "RtlGetVersion") : NULL;
    OSVERSIONINFOW vi;
    memset(&vi, 0, sizeof vi);
    vi.dwOSVersionInfoSize = sizeof vi;
    if (!ver || ver(&vi) != 0) { snprintf(out, cap, "Windows (version unknown)"); return; }
    const char* name = vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000 ? "Windows 11"
                     : vi.dwMajorVersion == 10 ? "Windows 10" : "Windows";
    int n = snprintf(out, cap, "%s (%lu.%lu.%lu)", name, (unsigned long)vi.dwMajorVersion,
                     (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber);
    /* Wine (and Proton) say so through ntdll's wine_get_version, and the host system through wine_get_host_version */
    typedef const char* (*wine_ver_fn)(void);
    typedef void (*wine_host_fn)(const char**, const char**);
    wine_ver_fn wv = nt ? (wine_ver_fn)(void*)GetProcAddress(nt, "wine_get_version") : NULL;
    wine_host_fn wh = nt ? (wine_host_fn)(void*)GetProcAddress(nt, "wine_get_host_version") : NULL;
    if (wv && n > 0 && (size_t)n < cap) {
        const char *sys = NULL, *rel = NULL;
        if (wh) wh(&sys, &rel);
        if (sys) snprintf(out + n, cap - (size_t)n, " under Wine %s on %s %s", wv(), sys, rel ? rel : "");
        else snprintf(out + n, cap - (size_t)n, " under Wine %s", wv());
    }
}

static void cores_line(char* out, size_t cap)
{
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    unsigned cores = 0, threads = 0;
    char* buf = len ? (char*)malloc(len) : NULL;
    if (buf && GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf,
                                                &len)) {
        for (DWORD off = 0; off < len;) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf + off);
            if (p->Relationship == RelationProcessorCore) {
                cores++;
                for (WORD g = 0; g < p->Processor.GroupCount; g++)
                    threads += (unsigned)__builtin_popcountll((unsigned long long)p->Processor.GroupMask[g].Mask);
            }
            off += p->Size;
        }
    }
    free(buf);
    if (!threads) { SYSTEM_INFO si; GetSystemInfo(&si); threads = si.dwNumberOfProcessors; }
    if (cores) snprintf(out, cap, "%u cores, %u threads", cores, threads);
    else snprintf(out, cap, "%u threads", threads);
}

static void memory_line(char* out, size_t cap)
{
    MEMORYSTATUSEX m;
    m.dwLength = sizeof m;
    if (!GlobalMemoryStatusEx(&m)) { snprintf(out, cap, "unknown"); return; }
    snprintf(out, cap, "%.1f GB, %.1f GB available", m.ullTotalPhys / 1073741824.0, m.ullAvailPhys / 1073741824.0);
}
#else
static void os_line(char* out, size_t cap)
{
    char pretty[256] = "";
    FILE* f = fopen("/etc/os-release", "r");
    if (!f) f = fopen("/usr/lib/os-release", "r");
    if (f) {
        char l[512];
        while (fgets(l, sizeof l, f)) {
            if (strncmp(l, "PRETTY_NAME=", 12) != 0) continue;
            char* v = l + 12;
            v[strcspn(v, "\r\n")] = 0;
            size_t n = strlen(v);
            if (n >= 2 && (v[0] == '"' || v[0] == '\'') && v[n - 1] == v[0]) { v[n - 1] = 0; v++; }
            snprintf(pretty, sizeof pretty, "%.255s", v);
        }
        fclose(f);
    }
    struct utsname u;
    if (uname(&u) == 0)
        snprintf(out, cap, "%s (%s %s %s)", pretty[0] ? pretty : "Linux", u.sysname, u.release, u.machine);
    else
        snprintf(out, cap, "%s", pretty[0] ? pretty : "Linux");
}

static void cores_line(char* out, size_t cap)
{
    const long threads = sysconf(_SC_NPROCESSORS_ONLN);
    /* cores: the distinct (physical id, core id) pairs in /proc/cpuinfo */
    unsigned pairs[1024];
    unsigned np = 0, phys = 0;
    FILE* f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char l[256];
        while (fgets(l, sizeof l, f)) {
            unsigned v;
            if (sscanf(l, "physical id : %u", &v) == 1) phys = v;
            else if (sscanf(l, "core id : %u", &v) == 1) {
                const unsigned key = phys << 16 | (v & 0xFFFFu);
                unsigned i = 0;
                while (i < np && pairs[i] != key) i++;
                if (i == np && np < 1024) pairs[np++] = key;
            }
        }
        fclose(f);
    }
    if (np) snprintf(out, cap, "%u cores, %ld threads", np, threads);
    else snprintf(out, cap, "%ld threads", threads);
}

static void memory_line(char* out, size_t cap)
{
    unsigned long long total = 0, avail = 0;
    FILE* f = fopen("/proc/meminfo", "r");
    if (f) {
        char l[256];
        while (fgets(l, sizeof l, f)) {
            unsigned long long v;
            if (sscanf(l, "MemTotal: %llu kB", &v) == 1) total = v;
            else if (sscanf(l, "MemAvailable: %llu kB", &v) == 1) avail = v;
        }
        fclose(f);
    }
    if (!total) { snprintf(out, cap, "unknown"); return; }
    snprintf(out, cap, "%.1f GB, %.1f GB available", total / 1048576.0, avail / 1048576.0);
}
#endif

void kz_sys_log(void)
{
    char b[2048];
    os_line(b, sizeof b);     line("os", b);
    cpu_brand(b, sizeof b);   line("cpu", b);
    cores_line(b, sizeof b);  line("cores", b);
    memory_line(b, sizeof b); line("memory", b);
#ifdef _WIN32
    kz_sys_env_line(_environ, b, sizeof b);
#else
    kz_sys_env_line(environ, b, sizeof b);
#endif
    kz_sys_log_line("env", b);
    /* KZ_STARTED_BY (default unset: unknown): what started the game, set by the launcher */
    const char* s = getenv("KZ_STARTED_BY");
    size_t n = 0;
    for (; s && s[n] && n < 32; n++) b[n] = (s[n] == ' ' || (unsigned char)s[n] < 0x20) ? '_' : s[n];
    b[n] = 0;
    line("started-by", n ? b : "unknown");
}

/* ------------------------------------------------------------------------------------------------------------------
 * Displays: SDL2, linked through ps3recomp, looked up at run time (nothing else in the build calls these, so a weak
 * reference would stay unresolved against SDL2.dll's import library)
 * ----------------------------------------------------------------------------------------------------------------*/
void* kz_sdl_fn(const char* name)
{
#ifdef _WIN32
    HMODULE m = GetModuleHandleA("SDL2.dll");
    return m ? (void*)GetProcAddress(m, name) : NULL;
#else
    return dlsym(RTLD_DEFAULT, name);
#endif
}

typedef struct { uint32_t format; int w, h, refresh_rate; void* driverdata; } kz_sdl_mode;   /* SDL_DisplayMode */

void kz_sys_log_displays(void)
{
    int (*num)(void) = (int (*)(void))kz_sdl_fn("SDL_GetNumVideoDisplays");
    int (*mode)(int, kz_sdl_mode*) = (int (*)(int, kz_sdl_mode*))kz_sdl_fn("SDL_GetCurrentDisplayMode");
    if (!num || !mode) return;
    const int n = num();
    for (int i = 0; i < n && i < 16; i++) {
        kz_sdl_mode m;
        memset(&m, 0, sizeof m);
        if (mode(i, &m) != 0) continue;
        char b[96];
        int k = snprintf(b, sizeof b, "%dx%d@%dHz", m.w, m.h, m.refresh_rate);
#ifdef _WIN32
        /* the scale Windows' display settings show: the monitor's DPI over 96 (SDL asks GetDpiForMonitor; the game
         * is per-monitor DPI aware, win_dpi.h). Linux has no one answer (X11's DPI is not the desktop's scale). */
        int (*dpi)(int, float*, float*, float*) = (int (*)(int, float*, float*, float*))kz_sdl_fn("SDL_GetDisplayDPI");
        float hd = 0;
        if (dpi && dpi(i, NULL, &hd, NULL) == 0 && hd > 0 && k > 0)
            snprintf(b + k, sizeof b - (size_t)k, " scale %d%%", (int)(hd / 96.0f * 100.0f + 0.5f));
#else
        (void)k;
#endif
        line("display", b);
    }
}
