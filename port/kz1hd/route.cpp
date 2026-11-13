/*
 * Killzone 1 HD -- routes: a play recorded tick by tick and replayed through
 * the game's own input, for tests that see the same thing on every run and
 * for frame-rate benchmarks (a timedemo).
 *
 *   KZ_ROUTE_REC=<file>   record from the player's first tick (a level or a
 *                         checkpoint just loaded) until the game quits
 *   KZ_ROUTE_PLAY=<file>  replay it from the player's first tick; the run
 *                         has to get there the same way (same save, same
 *                         menu path, same KZ_TICK_HZ)
 *   KZ_ROUTE_EXIT=1       quit when the replay ends or desyncs (exit status
 *                         0 or 2), after the summary line
 *   KZ_ROUTE_LOOSE=1      replay the whole recorded input even after the game
 *                         drifted from it, the player in God mode (a timedemo)
 *   KZ_ROUTE_LOOP=<n>     replay the route n times in a row (a soak test),
 *                         loose (as KZ_ROUTE_LOOSE=1): from the second time
 *                         on, the game is wherever the last one left it.
 *                         Each time ends with its own summary
 *   KZ_ROUTE_PACE=1       replay no faster than the recording (each frame
 *                         waits for the time it started at when recorded)
 *   KZ_ROUTE_REBASE=<file> with KZ_ROUTE_PLAY: also write what this replay
 *                         gave the game and found as a new route (rebasing a
 *                         played recording onto this build; route_rebase.h).
 *                         Replay the result loose, as it was made (God mode).
 *   KZ_ROUTE_SHOTS=<file>,<n>  every n frames of the route, create <file>
 *                         for one frame: with PS3RECOMP_VK_DUMP_TRIGGER=<file>
 *                         the frames dumped are the same ones in every run
 *
 * The file is text, one event per line, in the order the game asks for them:
 *
 *   F n now prev ms r r2  a frame: the ticks it advances, the clock's tick counts
 *                   (native_ppu/clock.cpp) and when it started (ms after the
 *                   first). Replayed frames do not wait for the timer: the
 *                   game time per frame is the recorded one and the game runs
 *                   as fast as the host draws.
 *   P ret hex       a read of the pad device (func_003D48F0, vtable +0x14 of
 *                   the player controller's input device): its return value
 *                   and the 0x24 bytes of state it filled in ("=": the same
 *                   bytes as the last P). Keys and mouse buttons reach the
 *                   game through it (cellPad's key map).
 *   M dx dy         a player tick's mouse look, when the mouse moved
 *   S x y z r       after a player tick: the player's position (hex floats)
 *                   and the game's random number generator, compared on replay
 *   H key value     the header: tick_hz, and rng and rng2 (the random generators
 *                   as the route starts, restored on replay); # starts a comment
 *
 * A replay that meets an event other than the one the game asks for has
 * desynced: it stops, says where, and the game is live again. A position
 * that differs is reported once and counted, with the largest difference;
 * the replay goes on. So is the first difference in the random generator.
 *
 * How exact a replay is (2026-10-31, docs/rotas.md): with both of the game's
 * random generators restored as the route starts (H rng, H rng2: their seeds
 * depended on the menus' timing) and the sounds on a generator of their own
 * (sound_variant), a Level 1 combat route of 6369 frames matched in every
 * frame; another drifted 1.4 cm by its end. Before, up to 2 m. Then
 * (2026-11-01) the sound mixed on the route's clock (ps3recomp patch 0106,
 * in kz_route_clock): AI agents wait for actions whose end followed the
 * sound mixed in real time. 4 of 5 Level 1 combat routes of ~11000 frames
 * replay unpaced with the player exact to the end, 3 with both generators
 * too; the others part after ~9000 frames, cause not found. A replay is a
 * fixed game time per frame and fixed input, not a bit-exact rerun of the
 * whole world.
 */

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <cmath>
#include <chrono>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>

#include "ppu_recomp.h"
#include "route.h"
#include "route_rebase.h"

extern "C" void kz_perf_begin(void);             /* perf.cpp */
extern "C" void kz_perf_report(const char* tag);

extern "C" {
extern volatile int g_kz_tick_hz;   /* main.cpp */
void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
void kz_quit(const char* why);
void ps3_audio_set_clock_ms(double ms);   /* ps3recomp cellAudio.c */
int  ps3_audio_clock_caught_up(void);
}

/* the route's frame, for a debugger's conditions */
extern "C" { volatile unsigned kz_route_frame; }

namespace {

constexpr uint32_t kPadBytes = KZ_ROUTE_PAD_BYTES;
/* the player's position: the controller's player (+0x18), the translation
 * row of its world matrix (+0xA0) at +0xD0 (measured: x and y follow the
 * walk, z the ground) */
constexpr uint32_t kPlayer = 0x18, kPos = 0xD0;
/* the state of the game's random number generator, x * 1664525 + 1013904223
 * (inlined at ~130 sites; 0x00013B48, 0x0029C030, the effects at 0x000DBB00) */
constexpr uint32_t kRng[] = {0x012D8018};
constexpr int kNRng = sizeof kRng / sizeof kRng[0];
/* a second generator, the same LCG, in the object 0x008285A4 points to, at
 * +0x2168 (the camera's idle sway, func_002F5C38, among others) */
uint32_t rng2_ea() { const uint32_t p = vm_read32(0x008285A4); return p ? p + 0x2168 : 0; }
uint32_t rng2() { const uint32_t ea = rng2_ea(); return ea ? vm_read32(ea) : 0; }

struct Event {
    char type;
    int line;
    int32_t a, b, c;           /* F: n now prev; P: ret; M: dx dy */
    double ms;                 /* F: when the frame started, after the first one */
    uint32_t frng, frng2; bool has_frng, has_frng2;   /* F: the random generators as the frame starts */
    uint32_t pos[3];           /* S */
    uint32_t rng[kNRng];       /* S: the random number generator */
    int nrng;
    uint8_t pad[kPadBytes];    /* P */
};

/* KZ_ROUTE_LOOSE=1: a replay that meets another event than the game asks for
 * goes on instead of stopping (a timedemo of the whole recorded input), with
 * the player in God mode (kz_route_clock): a pad
 * read with none recorded gets the last recorded pad, a frame skips what the
 * recording had left of the last one, a missing position check is skipped. */
/* KZ_ROUTE_LOOP=<n>: n times in a row, loose */
const int s_loops = getenv("KZ_ROUTE_LOOP") ? atoi(getenv("KZ_ROUTE_LOOP")) : 0;
const bool s_loose = (getenv("KZ_ROUTE_LOOSE") && atoi(getenv("KZ_ROUTE_LOOSE"))) || s_loops > 1;
int s_loop;                    /* the loop being replayed, from 0 */
double s_ms_base;              /* the recorded ms of the loops before this one */
unsigned s_resyncs;
Event s_last_p;

enum Mode { OFF, REC, PLAY };
Mode s_mode = OFF;
bool s_armed;                  /* from the player's first tick */
/* KZ_ROUTE_AUDIO_CLOCK=0: a replay mixes the sound in real time, as before */
const bool s_audio_clock = !getenv("KZ_ROUTE_AUDIO_CLOCK") || atoi(getenv("KZ_ROUTE_AUDIO_CLOCK"));
std::mutex s_lock;
FILE* s_out;
kz_route_writer s_w;           /* the recording's writer (s_out), or the rebase's (KZ_ROUTE_REBASE) */
bool s_rebase;                 /* a replay that also writes s_w */
std::string s_rebase_path;
std::string s_path;
std::vector<Event> s_ev;
size_t s_at;
uint8_t s_last_pad[kPadBytes];
bool s_have_last;
int s_hz;                      /* the header's tick rate */
uint32_t s_rng0, s_rng2;       /* the header's random generators at the start */
bool s_have_rng2;
bool s_have_rng0;
unsigned s_frames, s_bad_pos;
float s_max_off;               /* the largest position difference, in metres */
std::vector<double> s_frame_ms;
std::chrono::steady_clock::time_point s_t0, s_tf;

double ms_since(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

uint32_t rd32(uint32_t ea) { return vm_read32(ea); }


void summary(const char* how)
{
    char tag[32];
    if (s_loops > 1) snprintf(tag, sizeof tag, "loop %d", s_loop + 1);
    else snprintf(tag, sizeof tag, "replay");
    kz_perf_report(tag);
    const double secs = ms_since(s_t0) / 1000.0;
    std::vector<double> t = s_frame_ms;
    std::sort(t.begin(), t.end());
    double low = 0;   /* 1% low: the frame rate of the slowest 1% of frames */
    if (!t.empty()) {
        const size_t k = std::max<size_t>(1, t.size() / 100);
        double sum = 0;
        for (size_t i = t.size() - k; i < t.size(); i++) sum += t[i];
        low = 1000.0 * k / sum;
    }
    fprintf(stderr, "[route] %s %s: %u frames in %.2f s = %.1f fps, 1%% low %.1f fps, "
            "%u position differences (at most %.3g), %zu of %zu events%s\n",
            tag, how, s_frames, secs, secs > 0 ? s_frames / secs : 0.0, low, s_bad_pos, (double)s_max_off, s_at, s_ev.size(),
            s_resyncs ? " (loose: desynced, the rest replayed as input only)" : "");
}

extern "C" void kz_validation_summary(void);   /* main.cpp */
void stop_replay(const char* how, int status)
{
    summary(how);
    if (s_rebase) { fclose(s_w.f); s_rebase = false; }
    s_mode = OFF;
    ps3_audio_set_clock_ms(-1.0);
    static int exit_on = -1;
    if (exit_on < 0) exit_on = getenv("KZ_ROUTE_EXIT") && atoi(getenv("KZ_ROUTE_EXIT")) ? 1 : 0;
    if (exit_on) {
        if (status) fprintf(stderr, "[route] exit status %d\n", status);
        fflush(NULL);
        if (status) { kz_validation_summary(); _exit(status); }
        kz_quit("route replayed");
    }
}

const char* describe(char t)
{
    switch (t) {
    case 'F': return "a frame"; case 'P': return "a pad read";
    case 'M': return "a mouse move"; case 'S': return "a position"; default: return "the end";
    }
}

/* KZ_ROUTE_LOOP: the route ended; true when it starts again */
bool next_loop()
{
    if (s_loop + 1 >= s_loops) return false;
    summary("ended");
    for (size_t i = s_ev.size(); i-- > 0;) if (s_ev[i].type == 'F' && s_ev[i].ms > 0) { s_ms_base += s_ev[i].ms; break; }
    s_loop++;
    s_at = 0; s_frames = 0; s_bad_pos = 0; s_max_off = 0; s_resyncs = 0;
    s_frame_ms.clear();
    s_t0 = std::chrono::steady_clock::now();
    kz_perf_begin();
    return true;
}

/* The next event, which has to be of type t; otherwise the replay desyncs. */
Event* expect(char t)
{
    if (s_at >= s_ev.size() && !next_loop()) { stop_replay("ended", 0); return nullptr; }
    Event* e = &s_ev[s_at];
    if (e->type != t && s_loose) {
        if (!s_resyncs++)
            fprintf(stderr, "[route] first desync at line %d after %u frames (the game asked for %s, the route has %s): going on loose\n",
                    e->line, s_frames, describe(t), describe(e->type));
        /* pad reads with no frame between them (a menu after the player died):
         * the recorded play cannot go on */
        static unsigned stuck;
        if (t != 'P') stuck = 0;
        else if (++stuck > 2000) { stop_replay("stuck: pad reads without frames (loose)", 3); return nullptr; }
        if (t == 'P') return s_last_p.type == 'P' ? &s_last_p : nullptr;
        if (t == 'S') return nullptr;
        while (s_at < s_ev.size() && s_ev[s_at].type != t) s_at++;
        if (s_at >= s_ev.size()) {
            if (!next_loop()) { stop_replay("ended", 0); return nullptr; }
            while (s_at < s_ev.size() && s_ev[s_at].type != t) s_at++;
        }
        e = &s_ev[s_at];
    }
    if (e->type != t) {
        fprintf(stderr, "[route] desync at line %d after %u frames: the game asked for %s, the route has %s\n",
                e->line, s_frames, describe(t), describe(e->type));
        stop_replay("desynced", 2);
        return nullptr;
    }
    s_at++;
    if (t == 'P') s_last_p = *e;
    return e;
}

/* KZ_ROUTE_SHOTS: the trigger file exists on every n-th frame only. */
void shots()
{
    static int every = -1;
    static char path[512];
    if (every < 0) {
        const char* e = getenv("KZ_ROUTE_SHOTS");
        const char* c = e ? strrchr(e, ',') : nullptr;
        every = c ? atoi(c + 1) : 0;
        if (every > 0) snprintf(path, sizeof path, "%.*s", (int)(c - e), e);
    }
    if (every <= 0) return;
    if (s_frames % every == 0) { if (FILE* f = fopen(path, "w")) fclose(f); }
    else remove(path);
}

bool load(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[route] cannot open %s\n", path); return false; }
    char buf[256];
    int line = 0;
    while (fgets(buf, sizeof buf, f)) {
        line++;
        Event e{};
        e.type = buf[0]; e.line = line;
        char hex[2 * kPadBytes + 8];
        bool ok = true;
        switch (buf[0]) {
        case '#': case '\n': case '\r': continue;
        case 'H': { char k[32], v[32];
            if (sscanf(buf + 1, "%31s %31s", k, v) == 2) {
                if (!strcmp(k, "tick_hz")) s_hz = atoi(v);
                if (!strcmp(k, "rng")) { s_rng0 = (uint32_t)strtoul(v, nullptr, 16); s_have_rng0 = true; }
                if (!strcmp(k, "rng2")) { s_rng2 = (uint32_t)strtoul(v, nullptr, 16); s_have_rng2 = true; }
            }
            continue; }
        case 'F': ok = sscanf(buf + 1, "%d %d %d", &e.a, &e.b, &e.c) == 3; e.ms = -1;
            { const int k = sscanf(buf + 1, "%*d %*d %*d %lf %x %x", &e.ms, &e.frng, &e.frng2);
              e.has_frng = k >= 2; e.has_frng2 = k >= 3; }
            break;
        case 'M': ok = sscanf(buf + 1, "%d %d", &e.a, &e.b) == 2; break;
        case 'S': ok = sscanf(buf + 1, "%x %x %x", &e.pos[0], &e.pos[1], &e.pos[2]) == 3;
            e.nrng = sscanf(buf + 1, "%*x %*x %*x %x", &e.rng[0]); if (e.nrng < 0) e.nrng = 0; break;
        case 'P':
            ok = sscanf(buf + 1, "%d %72s", &e.a, hex) == 2;
            if (ok && hex[0] == '=') {
                ok = s_have_last;
                memcpy(e.pad, s_last_pad, kPadBytes);
            } else if (ok) {
                ok = strlen(hex) == 2 * kPadBytes;
                for (uint32_t i = 0; ok && i < kPadBytes; i++) { unsigned v; ok = sscanf(hex + 2 * i, "%2x", &v) == 1; e.pad[i] = (uint8_t)v; }
                memcpy(s_last_pad, e.pad, kPadBytes); s_have_last = true;
            }
            break;
        default: ok = false;
        }
        if (!ok) { fprintf(stderr, "[route] %s line %d: cannot read \"%.40s\"\n", path, line, buf); fclose(f); return false; }
        s_ev.push_back(e);
    }
    fclose(f);
    s_have_last = false;
    fprintf(stderr, "[route] %s: %zu events, recorded at %d Hz\n", path, s_ev.size(), s_hz);
    return true;
}

/* The sounds' own random generator (kz_route_sound_rng): the same LCG, its
 * state kept here and swapped into the game's word around a sound's draws. */
uint32_t s_sound_rng, s_game_rng;
int s_sound_depth;

/* func_001A5380(sound r3): picks one of a sound's variants at random among
 * those the sound system has resident (0x00083D5C), asking for the others to
 * load (0x00083DF8). See native_ppu/sound_rng.cpp. */
void sound_variant(ppu_context* ctx)
{
    kz_route_sound_rng(1);
    func_001A5380(ctx);
    kz_route_sound_rng(0);
}

/* func_003D48F0(device r3, state r4): the pad device's read. */
void pad_read(ppu_context* ctx)
{
    const uint32_t out = (uint32_t)ctx->gpr[4];
    func_003D48F0(ctx);
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_armed) return;
    if (s_mode == PLAY) {
        if (Event* e = expect('P')) {
            for (uint32_t i = 0; i < kPadBytes; i++) vm_write8(out + i, e->pad[i]);
            ctx->gpr[3] = (uint64_t)(int64_t)e->a;
        }
    }
    /* a recording, or a rebase: what the game got, read back */
    if (s_mode == REC || (s_mode == PLAY && s_rebase)) {
        uint8_t b[kPadBytes];
        for (uint32_t i = 0; i < kPadBytes; i++) b[i] = vm_read8(out + i);
        kz_route_w_pad(&s_w, (int32_t)ctx->gpr[3], b);
    }
}

}  // namespace

extern "C" int kz_route_clock(uint32_t clock, int32_t* n, int32_t* now, int32_t* prev)
{
    (void)clock;
    if (s_mode != PLAY || !s_armed) return 0;
    std::lock_guard<std::mutex> g(s_lock);
    /* timed from the first frame: before it the level loads */
    if (s_frames) s_frame_ms.push_back(ms_since(s_tf));
    else { s_t0 = std::chrono::steady_clock::now(); kz_perf_begin(); }
    s_tf = std::chrono::steady_clock::now();
    Event* e = expect('F');
    if (!e) return 0;
    shots();
    s_frames++;
    *n = e->a; *now = e->b; *prev = e->c;
    /* KZ_ROUTE_LOOSE: the game's own God mode (the "Overrides" settings of
     * the pause menu's debug options, the byte at +0x131 of the settings at
     * *0x00828734, read by the damage code func_002205F0): a replay that
     * drifted from its recording does not end with the player dead */
    if (s_loose) if (const uint32_t set = rd32(0x00828734)) vm_write8(set + 0x131, 1);
    /* The sound mixes on the recording's clock, up to when the next frame
     * started, and the frame waits for the mixer (at most 100 ms). Mixed in
     * real time, an AI agent's wait for its action to end (agent +0x3F0,
     * func_00137158) ended in other frames at another frame rate, and the
     * agents decided differently (measured, docs/rotas.md). */
    if (s_audio_clock) {
        double next = e->ms;
        for (size_t i = s_at; i < s_ev.size() && i < s_at + 64; i++) if (s_ev[i].type == 'F') { next = s_ev[i].ms; break; }
        ps3_audio_set_clock_ms(s_ms_base + next);
        for (int i = 0; i < 1000 && !ps3_audio_clock_caught_up(); i++) std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    kz_route_frame = s_frames;
    static bool said;
    if (e->has_frng && !said && rd32(kRng[0]) != e->frng) { said = true;
        fprintf(stderr, "[route] random state differs as frame %u starts (line %d): recorded %08x, now %08x\n",
                s_frames - 1, e->line, e->frng, rd32(kRng[0])); }
    static bool said2;
    if (e->has_frng2 && !said2 && rng2() != e->frng2) { said2 = true;
        fprintf(stderr, "[route] second random state differs as frame %u starts (line %d): recorded %08x, now %08x\n",
                s_frames - 1, e->line, e->frng2, rng2()); }
    /* the frame as replayed, the generators where they are compared above */
    if (s_rebase) kz_route_w_frame(&s_w, e->a, e->b, e->c, e->ms, rd32(kRng[0]), rng2());
    /* KZ_ROUTE_PACE=1: no faster than the recording */
    static int pace = getenv("KZ_ROUTE_PACE") ? 1 : 0;
    if (pace && e->ms > 0) while (ms_since(s_t0) < e->ms) std::this_thread::sleep_for(std::chrono::microseconds(200));
    return 1;
}

extern "C" void kz_route_clock_done(uint32_t clock)
{
    if (s_mode != REC || !s_armed) return;
    std::lock_guard<std::mutex> g(s_lock);
    if (!s_frames) s_t0 = std::chrono::steady_clock::now();
    kz_route_w_frame(&s_w, (int32_t)rd32(clock + 0x68), (int32_t)rd32(clock + 0x6C), (int32_t)rd32(clock + 0x70),
                     ms_since(s_t0), rd32(kRng[0]), rng2());
    kz_route_frame = s_frames;
    shots();
    s_frames++;
}

extern "C" void kz_route_tick_begin(void)
{
    if (s_mode == OFF || s_armed) return;
    std::lock_guard<std::mutex> g(s_lock);
    const int hz = g_kz_tick_hz > 0 ? g_kz_tick_hz : 30;
    if (s_mode == REC) {
        kz_route_w_header(&s_w, s_path.c_str(), nullptr, hz, rd32(kRng[0]), rng2());
        fprintf(stderr, "[route] recording to %s from the player's first tick (%d Hz)\n", s_path.c_str(), hz);
    } else {
        if (s_hz && s_hz != hz) {
            fprintf(stderr, "[route] recorded at %d Hz and the game clock is at %d Hz: run with KZ_TICK_HZ=%d\n", s_hz, hz, s_hz);
            s_mode = OFF;
            return;
        }
        /* the random generator as it was when recording started: how far the
         * menus before the route advanced it depends on their timing */
        if (s_have_rng0) vm_write32(kRng[0], s_rng0);
        if (s_have_rng2 && rng2_ea()) vm_write32(rng2_ea(), s_rng2);
        fprintf(stderr, "[route] replaying from the player's first tick\n");
        if (s_rebase) {
            const std::string note = "rebased (KZ_ROUTE_REBASE) from a loose replay of " + s_path + ": replay it with KZ_ROUTE_LOOSE=1";
            kz_route_w_header(&s_w, s_rebase_path.c_str(), note.c_str(), hz, rd32(kRng[0]), rng2());
        }
    }
    s_sound_rng = 0x4B5A0001;   /* any fixed value: the same in the recording and the replay */
    s_armed = true;
}

extern "C" void kz_route_mouse(int* dx, int* dy)
{
    if (s_mode == OFF || !s_armed) return;
    std::lock_guard<std::mutex> g(s_lock);
    if (s_mode == PLAY) {
        *dx = *dy = 0;
        if (s_at < s_ev.size() && s_ev[s_at].type == 'M') { *dx = s_ev[s_at].a; *dy = s_ev[s_at].b; s_at++; }
    }
    if (s_mode == REC || s_rebase) kz_route_w_mouse(&s_w, *dx, *dy);
}

extern "C" void kz_route_tick_end(uint32_t controller)
{
    if (s_mode == OFF || !s_armed) return;
    std::lock_guard<std::mutex> g(s_lock);
    const uint32_t player = rd32(controller + kPlayer);
    uint32_t pos[3] = {0, 0, 0};
    if (player) for (int i = 0; i < 3; i++) pos[i] = rd32(player + kPos + 4 * i);
    if (s_mode == REC || s_rebase) kz_route_w_pos(&s_w, pos, rd32(kRng[0]));
    if (s_mode != PLAY) return;
    if (Event* e = expect('S')) {
        static bool said[kNRng];
        for (int i = 0; i < e->nrng; i++)
            if (!said[i] && rd32(kRng[i]) != e->rng[i]) { said[i] = true;
                fprintf(stderr, "[route] random state 0x%08X differs at line %d after %u frames: recorded %08x, now %08x\n",
                        kRng[i], e->line, s_frames, e->rng[i], rd32(kRng[i])); }
        if (memcmp(e->pos, pos, sizeof pos)) {
            float a[3], b[3];
            memcpy(a, e->pos, sizeof a); memcpy(b, pos, sizeof b);
            const float d = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
            if (!s_bad_pos++)
                fprintf(stderr, "[route] position differs at line %d after %u frames: recorded %.6f %.6f %.6f, now %.6f %.6f %.6f\n",
                        e->line, s_frames, a[0], a[1], a[2], b[0], b[1], b[2]);
            if (!(d <= s_max_off)) s_max_off = d;
        }
    }
}

extern "C" void kz_route_sound_rng(int enter)
{
    if (s_mode == OFF) return;
    if (enter) {
        if (s_sound_depth++ == 0) { s_game_rng = rd32(kRng[0]); vm_write32(kRng[0], s_sound_rng); }
    } else if (s_sound_depth > 0 && --s_sound_depth == 0) {
        s_sound_rng = rd32(kRng[0]);
        vm_write32(kRng[0], s_game_rng);
    }
}

extern "C" void kz_route_register(void)
{
    const char* rec = getenv("KZ_ROUTE_REC");
    const char* play = getenv("KZ_ROUTE_PLAY");
    if (rec && *rec) {
        s_out = fopen(rec, "w");
        if (!s_out) { fprintf(stderr, "[route] cannot write %s\n", rec); return; }
        s_w.f = s_out;
        s_path = rec;
        s_mode = REC;
    } else if (play && *play) {
        if (!load(play)) return;
        s_path = play;
        s_mode = PLAY;
        /* KZ_ROUTE_REBASE=<file>: this replay written out as a new route */
        if (const char* rb = getenv("KZ_ROUTE_REBASE"); rb && *rb) {
            if (s_loops > 1) fprintf(stderr, "[route] KZ_ROUTE_REBASE: not with KZ_ROUTE_LOOP; no rebase\n");
            else if (!(s_w.f = fopen(rb, "w"))) fprintf(stderr, "[route] cannot write %s; no rebase\n", rb);
            else { s_rebase = true; s_rebase_path = rb; fprintf(stderr, "[route] rebasing into %s\n", rb); }
        }
    } else return;
    ppu_register_function(0x003D48F0, pad_read);
    ppu_register_function(0x001A5380, sound_variant);
}
