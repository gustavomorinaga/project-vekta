/*
 * Killzone 1 HD -- frame measurement: the interval between presents (what
 * the player sees), its percentiles and histogram, each thread's CPU over the
 * same window and the process's memory. A window opens with kz_perf_begin and
 * is reported with kz_perf_report:
 *
 *   - a route replay (route.cpp): from its first frame to its summary line,
 *     each loop of KZ_ROUTE_LOOP its own window
 *   - KZ_PERF_EVERY=<secs>: every <secs> seconds of a plain run
 *
 * The report is a few "[perf]" lines on stderr. KZ_PERF_OUT=<file> also
 * appends each window's intervals, one per line in ms after a "# <tag>"
 * line, for tools/perf/frametimes.py (histogram, comparison of two runs).
 *
 * The histogram's edges are the frame times of common refresh rates (240,
 * 165, 144, 120, 90, 60 and 30 Hz). A stutter is an interval over twice the
 * window's median. The thread table is CPU time over wall time, from
 * /proc/self/task: 100% is a thread that never waited (docs/medicao.md).
 *
 * KZ_RENDERDOC=<n>[,<count>]: in a run started under RenderDoc
 * (renderdoccmd capture, or qrenderdoc), capture <count> frames (default 1)
 * from present to present, starting at frame n of a replayed route
 * (KZ_ROUTE_PLAY: the same frames in every run) or else at the n-th present.
 * Headless runs have no swapchain for RenderDoc to cut frames at.
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef __linux__
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#endif
#if defined(__linux__) && __has_include(<renderdoc_app.h>)
#include <renderdoc_app.h>
#define KZ_RENDERDOC 1
#endif

extern "C" volatile unsigned kz_route_frame;   /* route.cpp */

namespace {

using Clock = std::chrono::steady_clock;

std::mutex s_lock;
bool s_open;
Clock::time_point s_t0, s_last;
bool s_have_last;
std::vector<float> s_iv;       /* the window's present intervals, ms */

struct Cpu { int tid; std::string name; double secs; };
std::vector<Cpu> s_cpu0;

#ifdef __linux__
/* utime + stime of every thread, from /proc/self/task/<tid>/stat */
std::vector<Cpu> thread_cpu()
{
    std::vector<Cpu> v;
    static const double hz = (double)sysconf(_SC_CLK_TCK);
    DIR* d = opendir("/proc/self/task");
    if (!d) return v;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        char path[64], buf[1024];
        snprintf(path, sizeof path, "/proc/self/task/%s/stat", e->d_name);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        const size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        /* "tid (comm) state ..." -- comm may hold spaces and parentheses */
        char* l = strchr(buf, '(');
        char* r = strrchr(buf, ')');
        if (!l || !r) continue;
        unsigned long ut = 0, st = 0;
        /* after ") ": state, then fields 4..13, utime is field 14 */
        if (sscanf(r + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st) != 2) continue;
        v.push_back({atoi(buf), std::string(l + 1, r), (ut + st) / hz});
    }
    closedir(d);
    return v;
}

/* VmRSS, VmHWM (kB), Threads; the open descriptors */
void mem(long* rss, long* hwm, long* threads, long* fds)
{
    *rss = *hwm = *threads = *fds = 0;
    if (FILE* f = fopen("/proc/self/status", "r")) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            sscanf(line, "VmRSS: %ld", rss);
            sscanf(line, "VmHWM: %ld", hwm);
            sscanf(line, "Threads: %ld", threads);
        }
        fclose(f);
    }
    if (DIR* d = opendir("/proc/self/fd")) {
        while (dirent* e = readdir(d)) if (e->d_name[0] != '.') (*fds)++;
        closedir(d);
    }
}
#else
std::vector<Cpu> thread_cpu() { return {}; }
void mem(long* rss, long* hwm, long* threads, long* fds) { *rss = *hwm = *threads = *fds = 0; }
#endif

/* the fps of the slowest fraction of frames: their count over their time */
double low_fps(const std::vector<float>& sorted, double frac)
{
    const size_t k = std::max<size_t>(1, (size_t)(sorted.size() * frac));
    double sum = 0;
    for (size_t i = sorted.size() - k; i < sorted.size(); i++) sum += sorted[i];
    return sum > 0 ? 1000.0 * k / sum : 0;
}

float pct(const std::vector<float>& sorted, double p)
{
    return sorted[std::min(sorted.size() - 1, (size_t)(p * (sorted.size() - 1) + 0.5))];
}

void report_locked(const char* tag)
{
    const double secs = std::chrono::duration<double>(Clock::now() - s_t0).count();
    std::vector<float> t = s_iv;
    std::sort(t.begin(), t.end());
    if (t.empty()) { fprintf(stderr, "[perf] %s: no presents in %.1f s\n", tag, secs); return; }
    const float med = pct(t, 0.5);
    size_t stutters = 0;
    for (float x : t) stutters += x > 2 * med;
    fprintf(stderr, "[perf] %s: %zu presents in %.2f s = %.1f fps; ms p50 %.2f p90 %.2f p99 %.2f p99.9 %.2f max %.2f; "
            "1%% low %.1f fps, 0.1%% low %.1f fps; %zu stutters (> 2x median)\n",
            tag, t.size(), secs, t.size() / secs, med, pct(t, 0.9), pct(t, 0.99), pct(t, 0.999), t.back(),
            low_fps(t, 0.01), low_fps(t, 0.001), stutters);
    /* the edges: 240, 165, 144, 120, 90, 60, 30 Hz */
    static const float edge[] = {4.17f, 6.06f, 6.94f, 8.33f, 11.1f, 16.7f, 33.3f};
    constexpr int kE = sizeof edge / sizeof edge[0];
    size_t n[kE + 1] = {};
    for (float x : t) { int b = 0; while (b < kE && x >= edge[b]) b++; n[b]++; }
    std::string h;
    char buf[64];
    for (int b = 0; b <= kE; b++) {
        if (b == 0) snprintf(buf, sizeof buf, "<%.2f", edge[0]);
        else if (b == kE) snprintf(buf, sizeof buf, " | >=%.1f", edge[kE - 1]);
        else snprintf(buf, sizeof buf, " | %.2f-%.2f", edge[b - 1], edge[b]);
        h += buf;
        snprintf(buf, sizeof buf, " %.2f%%", 100.0 * n[b] / t.size());
        h += buf;
    }
    fprintf(stderr, "[perf] %s: histogram ms: %s\n", tag, h.c_str());

    /* each thread's CPU over the window; threads born in it count from 0 */
    std::vector<Cpu> now = thread_cpu();
    std::vector<std::pair<double, const Cpu*>> busy;
    for (const Cpu& c : now) {
        double before = 0;
        for (const Cpu& o : s_cpu0) if (o.tid == c.tid) { before = o.secs; break; }
        busy.push_back({(c.secs - before) / secs * 100.0, &c});
    }
    std::sort(busy.begin(), busy.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::string th;
    for (size_t i = 0; i < busy.size() && i < 10 && busy[i].first >= 1.0; i++) {
        snprintf(buf, sizeof buf, "%s%s/%d %.1f%%", i ? ", " : "", busy[i].second->name.c_str(), busy[i].second->tid, busy[i].first);
        th += buf;
    }
    if (!th.empty()) fprintf(stderr, "[perf] %s: thread CPU: %s\n", tag, th.c_str());
    long rss, hwm, threads, fds;
    mem(&rss, &hwm, &threads, &fds);
    if (rss) fprintf(stderr, "[perf] %s: memory RSS %.1f MB (peak %.1f), %ld threads, %ld fds\n",
                     tag, rss / 1024.0, hwm / 1024.0, threads, fds);

    if (const char* out = getenv("KZ_PERF_OUT")) {
        if (FILE* f = fopen(out, "a")) {
            fprintf(f, "# %s\n", tag);
            for (float x : s_iv) fprintf(f, "%.3f\n", x);
            fclose(f);
        }
    }
}

void begin_locked()
{
    s_iv.clear();
    s_iv.reserve(1u << 16);
    s_have_last = false;
    s_t0 = Clock::now();
    s_cpu0 = thread_cpu();
    s_open = true;
}

#ifdef KZ_RENDERDOC
/* KZ_RENDERDOC: called at each present, on the presenting thread */
void renderdoc_present()
{
    static RENDERDOC_API_1_1_2* api;
    static int state = -1;     /* -1 unread, 0 off, 1 waiting, 2 capturing, 3 done */
    static unsigned at, count = 1, presents, started;
    static bool by_route;
    if (state == 0 || state == 3) return;
    if (state < 0) {
        state = 0;
        const char* e = getenv("KZ_RENDERDOC");
        if (!e || !*e) return;
        at = (unsigned)atoi(e);
        if (const char* c = strchr(e, ',')) count = std::max(1, atoi(c + 1));
        by_route = getenv("KZ_ROUTE_PLAY") != nullptr;
        void* lib = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
        auto get = lib ? (pRENDERDOC_GetAPI)dlsym(lib, "RENDERDOC_GetAPI") : nullptr;
        if (!get || !get(eRENDERDOC_API_Version_1_1_2, (void**)&api)) {
            fprintf(stderr, "[perf] KZ_RENDERDOC: not running under RenderDoc\n");
            return;
        }
        state = 1;
    }
    presents++;
    if (state == 1 && (by_route ? kz_route_frame >= at && kz_route_frame > 0 : presents >= at)) {
        api->StartFrameCapture(nullptr, nullptr);
        started = presents; state = 2;
        fprintf(stderr, "[perf] RenderDoc: capturing %u frame(s) from present %u (route frame %u)\n", count, presents, kz_route_frame);
    } else if (state == 2 && presents - started >= count) {
        api->EndFrameCapture(nullptr, nullptr);
        state = 3;
        fprintf(stderr, "[perf] RenderDoc: capture done\n");
    }
}
#else
void renderdoc_present() {}
#endif

}  // namespace

/* main.cpp, each guest frame handed to the backend */
extern "C" void kz_perf_present(void)
{
    renderdoc_present();
    if (!s_open) return;
    const Clock::time_point now = Clock::now();
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_open) return;
    if (s_have_last) s_iv.push_back(std::chrono::duration<float, std::milli>(now - s_last).count());
    s_last = now; s_have_last = true;
}

extern "C" void kz_perf_begin(void)
{
    std::lock_guard<std::mutex> g(s_lock);
    begin_locked();
}

extern "C" void kz_perf_report(const char* tag)
{
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_open) return;
    report_locked(tag);
    s_open = false;
}

/* main.cpp at boot: KZ_PERF_EVERY=<secs>, a window every <secs> seconds */
extern "C" void kz_perf_init(void)
{
    const char* e = getenv("KZ_PERF_EVERY");
    const int every = e ? atoi(e) : 0;
    if (every <= 0) return;
    std::thread([every] {
        for (unsigned n = 1;; n++) {
            { std::lock_guard<std::mutex> g(s_lock); begin_locked(); }
            std::this_thread::sleep_for(std::chrono::seconds(every));
            char tag[32];
            snprintf(tag, sizeof tag, "window %u", n);
            std::lock_guard<std::mutex> g(s_lock);
            report_locked(tag);
        }
    }).detach();
}
