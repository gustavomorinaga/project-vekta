/*
 * cellSail for Killzone 1 HD: plays the game's AVI movies (MPEG-4 Part 2 video, MP3
 * audio) with FFmpeg, through the same calls, callbacks and adapter protocol the game
 * uses on the PS3. Replaces the toolkit's HLE for every SAIL import the game makes
 * (registered as context handlers, which take precedence).
 *
 * What the game expects, read from its own code (Ghidra):
 *  - Player calls complete through the player callback: (cbArg, major << 32 | minor,
 *    arg0, arg1). Major 2 = call completed, minor = call id, arg1 = the userParam the
 *    call was given (the game passes a CellSailFuture and FutureSets it from the
 *    callback). Major 3 = state changed, arg0 = new state. Its movie state machine
 *    (FUN_00061B90) needs RUNNING (6), then OPENED (4) when the stream ends.
 *  - The AVI database: GetMovieInfo (+0x1C scale, +0x20 rate, +0x24 length),
 *    GetStreamByIndex until it fails, AviStreamGetMediaType 'vids' / 'auds', SetEs.
 *  - Video frames go into buffers the game allocates through the graphics adapter's
 *    alloc callback (FUN_00061E44: (arg, size, count, out[]) -> count; size at most
 *    1280x720x4 + 0x80). GraphicsAdapterGetFrame returns >= 0x100 with a new frame:
 *    info +0 buffer, +0x10 pts. GetFormat: +0 coding (1 = RGBA), +4 width, +6 height,
 *    +8 pitch.
 *  - Audio: SoundAdapterGetFrame(adapter, 256, info) returns >= 0x100 with info +0
 *    buffer (256 stereo float frames, big-endian), +4 session, +8 tag, +0x10 pts;
 *    GetFormat +1 channels, +4 rate (the game mixes only 48 kHz).
 *
 * Movies are read from the VFS (tools/setup-vfs.sh extracts them from the
 * psarc); "x-cell-fs://source:Movies/Intro/Intro.avi" resolves case-insensitively
 * under PS3_GAME/USRDIR.
 */

#include "ppu_recomp.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}
#define KZ_FF_IMPL
#include "ffmpeg_dyn.h"
extern "C" {

extern uint8_t* vm_base;
extern const char* ppu_vfs_root;
uint32_t vm_read32(uint64_t a);
void     vm_write8(uint64_t a, uint8_t v);
void     vm_write16(uint64_t a, uint16_t v);
void     vm_write32(uint64_t a, uint32_t v);
void     vm_write64(uint64_t a, uint64_t v);
uint64_t ppu_guest_call(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
void     ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
}

namespace {

const int64_t  OK = 0;
const int64_t  ERR_INVALID = (int32_t)0x80610701;
const uint32_t SCRATCH = 0x03000000;             /* guest scratch: alloc out-array, audio */
const uint32_t AUDIO_BUF = SCRATCH + 0x100;      /* 256 frames x 2 ch x 4 bytes */
const int      AUDIO_RATE = 48000, AUDIO_BLOCK = 256;

enum { EV_CALL_COMPLETED = 2, EV_STATE_CHANGED = 3 };
enum { CALL_BOOT = 1, CALL_OPEN_STREAM = 2, CALL_CLOSE_STREAM = 3, CALL_START = 10, CALL_STOP = 11 };
enum { ST_BOOT_TRANSITION = 1, ST_CLOSED = 2, ST_OPEN_TRANSITION = 3, ST_OPENED = 4,
       ST_START_TRANSITION = 5, ST_RUNNING = 6, ST_STOP_TRANSITION = 7, ST_CLOSE_TRANSITION = 8 };

struct Frame { std::vector<uint8_t> rgba; double pts; };

/* One open movie: the demuxer, its decoders and a decode thread filling the queues. */
struct Movie {
    AVFormatContext* fmt = nullptr;
    int vstream = -1, astream = -1;
    AVCodecContext *vdec = nullptr, *adec = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    int width = 0, height = 0;

    std::mutex m;
    std::condition_variable cv;
    std::deque<Frame> frames;            /* decoded, not yet shown */
    std::deque<float> pcm;               /* 48 kHz stereo, interleaved */
    bool eof = false, quit = false, rewind = false;
    std::thread th;

    ~Movie() { stop(); avcodec_free_context(&vdec); avcodec_free_context(&adec);
               sws_freeContext(sws); swr_free(&swr); avformat_close_input(&fmt); }
    void stop() { { std::lock_guard<std::mutex> l(m); quit = true; } cv.notify_all();
                  if (th.joinable()) th.join(); }
};

struct Player {
    uint32_t self = 0, cb = 0, cb_arg = 0;
    uint32_t gfx_adapter = 0, gfx_alloc = 0, gfx_arg = 0;
    uint32_t snd_adapter = 0;
    std::string path;                    /* host path of the current descriptor */
    int audio_pick = 0;                  /* the game's choice among the audio streams */
    Movie* movie = nullptr;
    bool blank = false;                  /* no movie to play: one black frame stands in */
    bool running = false, paused = false, repeat = false, ended = false;
    std::chrono::steady_clock::time_point t0;
    double paused_at = 0;
    uint32_t buffers[4] = {}; int nbuf = 0, next_buf = 0;
    uint32_t audio_tag = 0;
    uint64_t audio_served = 0;           /* 256-frame blocks handed out since the clock started */
    double clock() const {
        if (paused) return paused_at;
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
};

Player g_player;                          /* the game plays one movie at a time */
std::map<uint32_t, std::string> g_desc;   /* descriptor handle -> host path */
std::map<uint32_t, uint32_t> g_db_desc;   /* database EA -> descriptor */
std::map<uint32_t, uint64_t> g_futures;   /* future EA -> result (absent = not set) */
uint32_t g_next_desc = 1;

void notify(uint32_t major, uint32_t minor, uint64_t arg0, uint64_t arg1 = 0)
{
    static const bool trace = getenv("KZ_SAIL_TRACE") != nullptr;
    if (trace) fprintf(stderr, "[kz-sail] notify %u/%u 0x%llX 0x%llX\n", major, minor,
                       (unsigned long long)arg0, (unsigned long long)arg1);
    if (g_player.cb)
        ppu_guest_call(g_player.cb, g_player.cb_arg, ((uint64_t)major << 32) | minor, arg0, arg1,
                       0, 0, 0, 0);
}
void call_done(uint32_t call, uint32_t via, uint32_t to, uint64_t user)
{
    notify(EV_STATE_CHANGED, 0, via);
    notify(EV_CALL_COMPLETED, call, 0, user);
    notify(EV_STATE_CHANGED, 0, to);
}

/* "x-cell-fs://source:Menu/x.avi" -> <vfs>/PS3_GAME/USRDIR/menu/X.avi, matching case. */
std::string resolve(std::string uri)
{
    namespace fs = std::filesystem;
    for (const char* p : {"x-cell-fs://", "source:"})
        if (uri.rfind(p, 0) == 0) uri = uri.substr(strlen(p));
    fs::path cur = fs::path(ppu_vfs_root) / "PS3_GAME" / "USRDIR";
    size_t pos = 0;
    while (pos <= uri.size()) {
        size_t slash = uri.find('/', pos);
        std::string part = uri.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        pos = slash == std::string::npos ? uri.size() + 1 : slash + 1;
        if (part.empty()) continue;
        std::error_code ec; bool found = false;
        for (auto& e : fs::directory_iterator(cur, ec))
            if (strcasecmp(e.path().filename().string().c_str(), part.c_str()) == 0) { cur = e.path(); found = true; break; }
        if (!found) return {};
    }
    return cur.string();
}

std::string guest_string(uint32_t ea)
{
    std::string s;
    for (uint32_t i = 0; ea && i < 1024; i++) { char c = (char)vm_base[ea + i]; if (!c) break; s += c; }
    return s;
}

AVCodecContext* open_decoder(AVFormatContext* fmt, int idx)
{
    if (idx < 0) return nullptr;
    const AVCodec* c = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id);
    AVCodecContext* d = c ? avcodec_alloc_context3(c) : nullptr;
    if (!d || avcodec_parameters_to_context(d, fmt->streams[idx]->codecpar) < 0 || avcodec_open2(d, c, nullptr) < 0) {
        avcodec_free_context(&d);
        return nullptr;
    }
    return d;
}

Movie* movie_open(const std::string& path)
{
    if (!kz_ff_load()) return nullptr;
    Movie* mv = new Movie;
    if (avformat_open_input(&mv->fmt, path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(mv->fmt, nullptr) < 0) {
        fprintf(stderr, "[kz-sail] cannot open %s\n", path.c_str());
        delete mv;
        return nullptr;
    }
    return mv;
}

void decode_thread(Movie* mv)
{
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    for (;;) {
        {
            std::unique_lock<std::mutex> l(mv->m);
            /* Keep a few frames and about a second of audio ahead, no more.
             * Reading on for audio also decodes the video interleaved with it,
             * so cap the frames too (each is 3.6 MB). */
            mv->cv.wait(l, [&] { return mv->quit || mv->rewind ||
                                        ((mv->frames.size() < 4 || mv->pcm.size() < AUDIO_RATE * 2) &&
                                         mv->frames.size() < 16 && !mv->eof); });
            if (mv->quit) break;
            if (mv->rewind) {
                av_seek_frame(mv->fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
                if (mv->vdec) avcodec_flush_buffers(mv->vdec);
                if (mv->adec) avcodec_flush_buffers(mv->adec);
                mv->rewind = false; mv->eof = false;
            }
        }
        if (av_read_frame(mv->fmt, pkt) < 0) {
            std::lock_guard<std::mutex> l(mv->m);
            mv->eof = true;
            continue;
        }
        AVCodecContext* dec = pkt->stream_index == mv->vstream ? mv->vdec
                            : pkt->stream_index == mv->astream ? mv->adec : nullptr;
        if (dec && avcodec_send_packet(dec, pkt) >= 0) {
            while (avcodec_receive_frame(dec, fr) >= 0) {
                double tb = av_q2d(mv->fmt->streams[pkt->stream_index]->time_base);
                double pts = fr->best_effort_timestamp == AV_NOPTS_VALUE ? 0 : fr->best_effort_timestamp * tb;
                if (dec == mv->vdec) {
                    Frame f; f.pts = pts; f.rgba.resize((size_t)mv->width * mv->height * 4);
                    uint8_t* dst[1] = { f.rgba.data() }; int pitch[1] = { mv->width * 4 };
                    sws_scale(mv->sws, fr->data, fr->linesize, 0, fr->height, dst, pitch);
                    std::lock_guard<std::mutex> l(mv->m);
                    mv->frames.push_back(std::move(f));
                } else {
                    int out_n = swr_get_out_samples(mv->swr, fr->nb_samples);
                    std::vector<float> out((size_t)out_n * 2);
                    uint8_t* o[1] = { (uint8_t*)out.data() };
                    int got = swr_convert(mv->swr, o, out_n, (const uint8_t**)fr->extended_data, fr->nb_samples);
                    std::lock_guard<std::mutex> l(mv->m);
                    if (got > 0) mv->pcm.insert(mv->pcm.end(), out.begin(), out.begin() + got * 2);
                }
            }
        }
        av_packet_unref(pkt);
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
}

/* ---- handlers -------------------------------------------------------------------- */

#define ARG(n) ctx->gpr[3 + (n)]
#define RET(v) (ctx->gpr[3] = (uint64_t)(int64_t)(v))
#define EA(n) ((uint32_t)ARG(n))

void ok(ppu_context* ctx) { RET(OK); }

void player_initialize2(ppu_context* ctx)
{
    g_player = Player{};
    g_player.self = EA(0); g_player.cb = EA(2); g_player.cb_arg = EA(3);
    RET(OK);
}
void player_set_graphics_adapter(ppu_context* ctx) { g_player.gfx_adapter = EA(2); RET(OK); }
void player_set_sound_adapter(ppu_context* ctx)    { g_player.snd_adapter = EA(2); RET(OK); }

/* GraphicsAdapterInitialize(adapter, funcs, arg): funcs = {makeup, cleanup,
 * formatChanged, alloc, free}, on the caller's stack, so read now. */
void graphics_adapter_initialize(ppu_context* ctx)
{
    g_player.gfx_alloc = vm_read32(EA(1) + 12);
    g_player.gfx_arg = EA(2);
    RET(OK);
}

void player_boot(ppu_context* ctx)
{
    call_done(CALL_BOOT, ST_BOOT_TRANSITION, ST_CLOSED, ARG(1));
    RET(OK);
}

void player_create_descriptor(ppu_context* ctx)
{
    std::string uri = guest_string(EA(3));
    uint32_t h = g_next_desc++;
    g_desc[h] = resolve(uri);
    fprintf(stderr, "[kz-sail] descriptor %u: %s -> %s\n", h, uri.c_str(),
            g_desc[h].empty() ? "(missing)" : g_desc[h].c_str());
    vm_write32(EA(4), h);
    RET(OK);
}
void player_destroy_descriptor(ppu_context* ctx) { g_desc.erase(EA(1)); RET(OK); }
void player_add_descriptor(ppu_context* ctx)
{
    auto it = g_desc.find(EA(1));
    if (it != g_desc.end()) g_player.path = it->second;
    RET(OK);
}

/* The database is where the game reads the container's own description from. */
void descriptor_create_database(ppu_context* ctx)
{
    g_db_desc[EA(1)] = EA(0);
    RET(g_desc.count(EA(0)) && !g_desc[EA(0)].empty() ? OK : ERR_INVALID);
}
std::map<uint32_t, Movie*> g_probes;   /* stream layout only, per database; closed with it */
void descriptor_destroy_database(ppu_context* ctx)
{
    auto p = g_probes.find(EA(1));
    if (p != g_probes.end()) { delete p->second; g_probes.erase(p); }
    g_db_desc.erase(EA(1));
    RET(OK);
}

Movie* db_movie(uint32_t db)
{
    auto& probes = g_probes;
    auto d = g_db_desc.find(db);
    if (d == g_db_desc.end()) return nullptr;
    auto& mv = probes[db];
    if (!mv) mv = movie_open(g_desc[d->second]);
    return mv;
}

void avi_movie_get_movie_info(ppu_context* ctx)
{
    Movie* mv = db_movie(EA(0));
    uint32_t info = EA(1);
    for (uint32_t o = 0; o < 0x40; o += 4) vm_write32(info + o, 0);
    if (!mv) { vm_write32(info + 0x20, 1); RET(ERR_INVALID); return; }
    int v = av_find_best_stream(mv->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVRational fr = v >= 0 ? mv->fmt->streams[v]->avg_frame_rate : AVRational{30, 1};
    int64_t frames = v >= 0 ? mv->fmt->streams[v]->nb_frames : 0;
    if (frames <= 0) frames = (int64_t)(mv->fmt->duration / 1e6 * av_q2d(fr));
    vm_write32(info + 0x0C, mv->fmt->nb_streams);
    vm_write32(info + 0x14, v >= 0 ? mv->fmt->streams[v]->codecpar->width : 0);
    vm_write32(info + 0x18, v >= 0 ? mv->fmt->streams[v]->codecpar->height : 0);
    vm_write32(info + 0x1C, fr.den);
    vm_write32(info + 0x20, fr.num ? fr.num : 1);
    vm_write32(info + 0x24, (uint32_t)frames);
    RET(OK);
}

/* Stream handle (16 bytes the game keeps on its stack): +0 database, +4 index. */
void avi_movie_get_stream_by_index(ppu_context* ctx)
{
    Movie* mv = db_movie(EA(0));
    if (!mv || EA(1) >= mv->fmt->nb_streams) { RET(ERR_INVALID); return; }
    vm_write32(EA(2), EA(0));
    vm_write32(EA(2) + 4, EA(1));
    RET(OK);
}
void avi_stream_get_media_type(ppu_context* ctx)
{
    Movie* mv = db_movie(vm_read32(EA(0)));
    uint32_t idx = vm_read32(EA(0) + 4);
    uint32_t type = 0;
    if (mv && idx < mv->fmt->nb_streams)
        type = mv->fmt->streams[idx]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ? 0x76696473u  /* vids */
                                                                                 : 0x61756473u; /* auds */
    vm_write32(EA(1), type);
    RET(OK);
}
/* SetEs(desc, type (1 video, 0 audio), ordinal, enable, stream). */
void descriptor_set_es(ppu_context* ctx)
{
    if (ARG(1) == 0) g_player.audio_pick = (int)vm_read32(EA(4) + 4);
    RET(OK);
}

void player_open_stream(ppu_context* ctx)
{
    delete g_player.movie;
    /* KZ_SKIP_INTRO (intro = off in kz1hd_pc.ini): the opening movie is a blank
     * stream, so the game goes from the logos straight to the main menu. */
    static const bool skip_intro = getenv("KZ_SKIP_INTRO") != nullptr;
    const bool intro = g_player.path == resolve("Movies/Intro/Intro.avi");
    Movie* mv = g_player.movie = g_player.path.empty() || (skip_intro && intro) ? nullptr : movie_open(g_player.path);
    if (mv) {
        mv->vstream = av_find_best_stream(mv->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        mv->astream = g_player.audio_pick > 0 && g_player.audio_pick < (int)mv->fmt->nb_streams &&
                      mv->fmt->streams[g_player.audio_pick]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
                    ? g_player.audio_pick : av_find_best_stream(mv->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        mv->vdec = open_decoder(mv->fmt, mv->vstream);
        mv->adec = open_decoder(mv->fmt, mv->astream);
        if (mv->vdec) {
            mv->width = mv->vdec->width; mv->height = mv->vdec->height;
            mv->sws = sws_getContext(mv->width, mv->height, mv->vdec->pix_fmt, mv->width, mv->height,
                                     AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        }
        if (mv->adec) {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            swr_alloc_set_opts2(&mv->swr, &stereo, AV_SAMPLE_FMT_FLT, AUDIO_RATE,
                                &mv->adec->ch_layout, mv->adec->sample_fmt, mv->adec->sample_rate, 0, nullptr);
            swr_init(mv->swr);
        }
        /* Frame buffers come from the game's own allocator (the adapter's alloc). */
        g_player.nbuf = 0;
        if (g_player.gfx_alloc && mv->vdec) {
            int64_t n = (int64_t)ppu_guest_call(g_player.gfx_alloc, g_player.gfx_arg,
                                                (uint64_t)mv->width * mv->height * 4, 3, SCRATCH, 0, 0, 0, 0);
            for (int i = 0; i < n && i < 4; i++) g_player.buffers[g_player.nbuf++] = vm_read32(SCRATCH + 4 * i);
        }
        fprintf(stderr, "[kz-sail] open %s: %dx%d video %d audio %d, %d frame buffers\n",
                g_player.path.c_str(), mv->width, mv->height, mv->vstream, mv->astream, g_player.nbuf);
        mv->th = std::thread(decode_thread, mv);
    }
    /* Nothing to play (no FFmpeg, KZ_NO_MOVIES, a missing file): a black frame at
     * the 1280x720 graphics_adapter_get_format reports. The main menu draws only
     * once its background movie has given a frame (all black without one). */
    g_player.blank = !mv;
    if (!mv) {
        g_player.nbuf = g_player.next_buf = 0;
        if (g_player.gfx_alloc &&
            (int64_t)ppu_guest_call(g_player.gfx_alloc, g_player.gfx_arg, 1280 * 720 * 4, 1, SCRATCH, 0, 0, 0, 0) > 0) {
            g_player.buffers[g_player.nbuf++] = vm_read32(SCRATCH);
            for (uint32_t o = 0; o < 1280 * 720 * 4; o += 4) vm_write32(g_player.buffers[0] + o, 0x000000FFu);
        }
    }
    call_done(CALL_OPEN_STREAM, ST_OPEN_TRANSITION, ST_OPENED, ARG(1));
    RET(OK);
}

void player_start(ppu_context* ctx)
{
    g_player.running = true; g_player.paused = false; g_player.ended = false;
    g_player.t0 = std::chrono::steady_clock::now(); g_player.audio_served = 0;
    call_done(CALL_START, ST_START_TRANSITION, ST_RUNNING, ARG(2));
    RET(OK);
}
void player_stop(ppu_context* ctx)
{
    g_player.running = false;
    if (g_player.movie) { delete g_player.movie; g_player.movie = nullptr; }
    call_done(CALL_STOP, ST_STOP_TRANSITION, ST_OPENED, ARG(1));
    RET(OK);
}
void player_set_repeat_mode(ppu_context* ctx) { g_player.repeat = ARG(1) != 0; RET(OK); }
void player_set_paused(ppu_context* ctx)
{
    bool p = ARG(1) != 0;
    if (p && !g_player.paused) { g_player.paused_at = g_player.clock(); g_player.paused = true; }
    else if (!p && g_player.paused) {
        g_player.paused = false;
        g_player.t0 = std::chrono::steady_clock::now() -
                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(g_player.paused_at));
    }
    RET(OK);
}
void player_finalize(ppu_context* ctx)
{
    delete g_player.movie; g_player.movie = nullptr;
    g_player.running = false;
    RET(OK);
}

/* End of stream: loop when repeating, otherwise report the player back in OPENED,
 * which is what the game's state machine waits for. Runs on the game's thread. */
void check_end(Movie* mv)
{
    bool done;
    { std::lock_guard<std::mutex> l(mv->m); done = mv->eof && mv->frames.empty(); }
    if (!done || g_player.ended) return;
    if (g_player.repeat) {
        { std::lock_guard<std::mutex> l(mv->m); mv->rewind = true; mv->pcm.clear(); }
        mv->cv.notify_all();
        g_player.t0 = std::chrono::steady_clock::now(); g_player.audio_served = 0;
        return;
    }
    g_player.ended = true;
    notify(EV_STATE_CHANGED, 0, ST_STOP_TRANSITION);
    notify(EV_STATE_CHANGED, 0, ST_OPENED);
}

/* The black frame of a blank stream: shown once, then the stream is over the way
 * a movie's end is reported (unless it repeats, like the menu's background).
 * Not from Start: the game's FUN_000619BC stores RUNNING (6) as the player state
 * after cellSailPlayerStart returns, overwriting an OPENED (4) sent during the
 * call, and its poll (FUN_00061B90) then waits for a 4 that never comes. */
void blank_frame(ppu_context* ctx)
{
    if (!g_player.running || g_player.ended || !g_player.nbuf) { RET(0); return; }
    if (g_player.next_buf++ && !g_player.repeat) {
        g_player.ended = true;
        notify(EV_STATE_CHANGED, 0, ST_STOP_TRANSITION);
        notify(EV_STATE_CHANGED, 0, ST_OPENED);
        RET(0);
        return;
    }
    uint64_t pts = (uint64_t)(g_player.clock() * 90000);
    vm_write32(EA(1), g_player.buffers[0]);
    vm_write32(EA(1) + 8, (uint32_t)pts);
    vm_write64(EA(1) + 0x10, pts);
    RET(0x100);
}

void graphics_adapter_get_frame(ppu_context* ctx)
{
    Movie* mv = g_player.movie;
    if (g_player.blank) { blank_frame(ctx); return; }
    if (!mv || !g_player.running || !g_player.nbuf) { RET(0); return; }
    double now = g_player.clock();
    Frame f; bool have = false;
    static unsigned calls, dropped;
    calls++;
    {
        std::lock_guard<std::mutex> l(mv->m);
        /* One frame per call; frames more than 0.2 s late are skipped. */
        while (mv->frames.size() > 1 && mv->frames[1].pts <= now - 0.2) { mv->frames.pop_front(); dropped++; }
        if (!mv->frames.empty() && mv->frames.front().pts <= now) {
            f = std::move(mv->frames.front()); mv->frames.pop_front(); have = true;
        }
    }
    mv->cv.notify_all();
    if (!have) { check_end(mv); RET(0); return; }
    static int dbg = getenv("KZ_SAIL_DBG") != nullptr;
    static unsigned shown;
    if (dbg && ++shown % 150 == 0)
        fprintf(stderr, "[kz-sail] frame pts %.2f clock %.2f, %u calls, %u dropped\n", f.pts, now, calls, dropped);
    uint32_t buf = g_player.buffers[g_player.next_buf++ % g_player.nbuf];
    memcpy(vm_base + buf, f.rgba.data(), f.rgba.size());
    uint32_t info = EA(1);
    vm_write32(info, buf);
    vm_write32(info + 8, (uint32_t)(f.pts * 90000));
    vm_write64(info + 0x10, (uint64_t)(f.pts * 90000));
    RET(0x100);
}
void graphics_adapter_get_format(ppu_context* ctx)
{
    Movie* mv = g_player.movie;
    uint32_t fmt = EA(1);
    for (uint32_t o = 0; o < 0x20; o += 4) vm_write32(fmt + o, 0);
    vm_write8(fmt, 1);                                   /* RGBA interleaved */
    vm_write16(fmt + 4, mv ? mv->width : 1280);
    vm_write16(fmt + 6, mv ? mv->height : 720);
    vm_write32(fmt + 8, (mv ? mv->width : 1280) * 4);
    RET(OK);
}
void pts_to_time_position(ppu_context* ctx) { vm_write64(EA(2), ARG(1)); RET(OK); }

void sound_adapter_get_frame(ppu_context* ctx)
{
    Movie* mv = g_player.movie;
    { static int on = -1; static unsigned n, empty; if (on < 0) on = getenv("KZ_SAIL_LOG") != nullptr;
      if (on && ++n % 200 == 1) fprintf(stderr, "[kz-sail] audio frame #%u (movie %d running %d paused %d pcm %zu)\n",
          n, mv != nullptr, (int)g_player.running, (int)g_player.paused, mv ? mv->pcm.size() : (size_t)0); (void)empty; }
    if (!mv || !g_player.running || g_player.paused) { RET(0); return; }
    /* One block per 256 frames of movie time, a few ahead: the game's audio
     * thread asks far more often than that and mixes each answer into the
     * block cellAudio plays next, so unpaced it drained the sound in a burst
     * and three blocks in four played silence. */
    if (g_player.audio_served >= (uint64_t)(g_player.clock() * AUDIO_RATE / AUDIO_BLOCK) + 4) { RET(0); return; }
    float block[AUDIO_BLOCK * 2];
    {
        std::lock_guard<std::mutex> l(mv->m);
        if (mv->pcm.size() < AUDIO_BLOCK * 2) { RET(0); return; }
        for (int i = 0; i < AUDIO_BLOCK * 2; i++) { block[i] = mv->pcm.front(); mv->pcm.pop_front(); }
    }
    mv->cv.notify_all();
    for (int i = 0; i < AUDIO_BLOCK * 2; i++) {
        uint32_t w; memcpy(&w, &block[i], 4);
        vm_write32(AUDIO_BUF + 4 * i, w);
    }
    uint32_t info = EA(2);
    vm_write32(info, AUDIO_BUF);
    vm_write32(info + 4, 1);
    vm_write32(info + 8, ++g_player.audio_tag);
    g_player.audio_served++;
    vm_write64(info + 0x10, (uint64_t)(g_player.clock() * 90000));
    RET(0x100);
}
void sound_adapter_get_format(ppu_context* ctx)
{
    uint32_t fmt = EA(1);
    for (uint32_t o = 0; o < 0x10; o += 4) vm_write32(fmt + o, 0);
    vm_write8(fmt + 1, 2);                               /* stereo */
    vm_write16(fmt + 2, AUDIO_BLOCK);
    vm_write32(fmt + 4, AUDIO_RATE);
    RET(OK);
}

void future_initialize(ppu_context* ctx) { g_futures.erase(EA(0)); RET(OK); }
void future_set(ppu_context* ctx)        { g_futures[EA(0)] = ARG(1); RET(OK); }
void future_reset(ppu_context* ctx)      { g_futures.erase(EA(0)); RET(OK); }
void future_get(ppu_context* ctx)
{
    auto it = g_futures.find(EA(0));
    if (it == g_futures.end()) { RET((int32_t)0x80610709); return; }   /* timeout */
    if (EA(2)) vm_write32(EA(2), (uint32_t)it->second);
    RET(OK);
}

}  // namespace

extern "C" void kz_sail_register(void)
{
    struct { uint32_t nid; const char* name; void (*fn)(ppu_context*); } t[] = {
        {0x23654375u, "cellSailPlayerInitialize2", player_initialize2},
        {0x18B4629Du, "cellSailPlayerFinalize", player_finalize},
        {0x18BCD21Bu, "cellSailPlayerSetGraphicsAdapter", player_set_graphics_adapter},
        {0x1139A206u, "cellSailPlayerSetSoundAdapter", player_set_sound_adapter},
        {0x5F7C7A6Fu, "cellSailPlayerSetParameter", ok},
        {0xBDF21B0Fu, "cellSailPlayerBoot", player_boot},
        {0xD7938B8Du, "cellSailPlayerCreateDescriptor", player_create_descriptor},
        {0xFC839BD4u, "cellSailPlayerDestroyDescriptor", player_destroy_descriptor},
        {0x7C8DFF3Bu, "cellSailPlayerAddDescriptor", player_add_descriptor},
        {0x34ECC1B9u, "cellSailPlayerOpenStream", player_open_stream},
        {0xE535B0D3u, "cellSailPlayerStart", player_start},
        {0xEBA8D4ECu, "cellSailPlayerStop", player_stop},
        {0xFC5BAF8Au, "cellSailPlayerSetRepeatMode", player_set_repeat_mode},
        {0xD1D55A90u, "cellSailPlayerSetPaused", player_set_paused},
        {0x0ABB318Bu, "cellSailDescriptorCreateDatabase", descriptor_create_database},
        {0x28336E89u, "cellSailDescriptorDestroyDatabase", descriptor_destroy_database},
        {0xC044FAB1u, "cellSailDescriptorOpen", ok},
        {0x15FD6A2Au, "cellSailDescriptorClose", ok},
        {0x76B1A425u, "cellSailDescriptorSetAutoSelection", ok},
        {0x0D0C2F0Cu, "cellSailDescriptorSetEs", descriptor_set_es},
        {0x6E83F5C0u, "cellSailAviMovieGetMovieInfo", avi_movie_get_movie_info},
        {0x3E908C56u, "cellSailAviMovieGetStreamByIndex", avi_movie_get_stream_by_index},
        {0xC09E2F23u, "cellSailAviStreamGetMediaType", avi_stream_get_media_type},
        {0x1C983864u, "cellSailGraphicsAdapterInitialize", graphics_adapter_initialize},
        {0x76488BB1u, "cellSailGraphicsAdapterFinalize", ok},
        {0x2E3CCB5Eu, "cellSailGraphicsAdapterSetPreferredFormat", ok},
        {0xFFD58AA4u, "cellSailGraphicsAdapterGetFormat", graphics_adapter_get_format},
        {0x0247C69Eu, "cellSailGraphicsAdapterGetFrame", graphics_adapter_get_frame},
        {0x1872331Bu, "cellSailGraphicsAdapterPtsToTimePosition", pts_to_time_position},
        {0x44A20E79u, "cellSailGraphicsAdapterUpdateAvSync", ok},
        {0x3D0D3B72u, "cellSailSoundAdapterInitialize", ok},
        {0xD1462438u, "cellSailSoundAdapterFinalize", ok},
        {0x1C9D5E5Au, "cellSailSoundAdapterSetPreferredFormat", ok},
        {0xF25F197Du, "cellSailSoundAdapterGetFormat", sound_adapter_get_format},
        {0x7EB8D6B5u, "cellSailSoundAdapterGetFrame", sound_adapter_get_frame},
        {0x4AE979DFu, "cellSailSoundAdapterPtsToTimePosition", pts_to_time_position},
        {0xEEC22809u, "cellSailSoundAdapterUpdateAvSync", ok},
        {0x346EBBA3u, "cellSailMemAllocatorInitialize", ok},
        {0x4CC54F8Eu, "cellSailFutureInitialize", future_initialize},
        {0x9553AF65u, "cellSailFutureFinalize", future_reset},
        {0xA37FED15u, "cellSailFutureSet", future_set},
        {0x3A2D806Cu, "cellSailFutureGet", future_get},
        {0x0C4CB439u, "cellSailFutureReset", future_reset},
    };
    /* KZ_SAIL_TRACE=1: every call by name, with the time since start (handlers are
     * looked up by NID, so the trace goes through one wrapper per slot). */
    static const char* names[64]; static void (*fns[64])(ppu_context*); static int n = 0;
    static const bool trace = getenv("KZ_SAIL_TRACE") != nullptr;
    #define W(i) [](ppu_context* c) { static unsigned k; if (k++ < 40 || k % 500 == 0) \
        fprintf(stderr, "[kz-sail] %.1f s %s #%u r3=0x%llX r4=0x%llX\n", std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(), \
                names[i], k, (unsigned long long)c->gpr[3], (unsigned long long)c->gpr[4]); fns[i](c); }
    static void (*const wrap[])(ppu_context*) = { W(0),W(1),W(2),W(3),W(4),W(5),W(6),W(7),W(8),W(9),W(10),W(11),W(12),W(13),W(14),W(15),
        W(16),W(17),W(18),W(19),W(20),W(21),W(22),W(23),W(24),W(25),W(26),W(27),W(28),W(29),W(30),W(31),W(32),W(33),W(34),W(35),
        W(36),W(37),W(38),W(39),W(40),W(41),W(42),W(43),W(44),W(45),W(46),W(47) };
    #undef W
    for (auto& e : t) {
        if (trace && n < 48) { names[n] = e.name; fns[n] = e.fn; ps3_hle_register_ctx(e.nid, e.name, wrap[n]); n++; }
        else ps3_hle_register_ctx(e.nid, e.name, e.fn);
    }
}
