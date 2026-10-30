/*
 * FFmpeg loaded at run time (sail.cpp's movies), the way ps3recomp's cellVdec
 * does it: building needs only the FFmpeg headers, and a machine without the
 * libraries (on Windows, the FFmpeg DLLs beside the exe) plays the game
 * without its movies instead of not starting. Include after the FFmpeg
 * headers; every call below then goes through kz_ff. kz_ff_load() is safe to
 * call from any thread and says whether the libraries are there.
 */
#ifndef KZ_FFMPEG_DYN_H
#define KZ_FFMPEG_DYN_H

/* library (0 avutil, 1 avcodec, 2 avformat, 3 swscale, 4 swresample), function */
#define KZ_FF_FNS(X) \
    X(0, av_frame_alloc) X(0, av_frame_free) \
    X(1, av_packet_alloc) X(1, av_packet_free) X(1, av_packet_unref) \
    X(1, avcodec_find_decoder) X(1, avcodec_alloc_context3) X(1, avcodec_free_context) \
    X(1, avcodec_parameters_to_context) X(1, avcodec_open2) X(1, avcodec_flush_buffers) \
    X(1, avcodec_send_packet) X(1, avcodec_receive_frame) \
    X(2, avformat_open_input) X(2, avformat_find_stream_info) X(2, avformat_close_input) \
    X(2, av_read_frame) X(2, av_seek_frame) X(2, av_find_best_stream) \
    X(3, sws_getContext) X(3, sws_scale) X(3, sws_freeContext) \
    X(4, swr_alloc_set_opts2) X(4, swr_init) X(4, swr_convert) X(4, swr_get_out_samples) X(4, swr_free)

struct KzFF {
#define KZ_FF_PTR(lib, fn) decltype(&::fn) fn;
    KZ_FF_FNS(KZ_FF_PTR)
#undef KZ_FF_PTR
};
extern KzFF kz_ff;
bool kz_ff_load();

#ifdef KZ_FF_IMPL
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <windows.h>
static void* kz_ff_open(const std::string& n) { return (void*)LoadLibraryA(n.c_str()); }
static void* kz_ff_sym(void* l, const char* s) { return (void*)GetProcAddress((HMODULE)l, s); }
#else
#include <dlfcn.h>
static void* kz_ff_open(const std::string& n) { return dlopen(n.c_str(), RTLD_NOW | RTLD_LOCAL); }
static void* kz_ff_sym(void* l, const char* s) { return dlsym(l, s); }
#endif
KzFF kz_ff;
bool kz_ff_load()
{
    static std::once_flag once;
    static bool ok;
    std::call_once(once, [] {
        if (getenv("KZ_NO_MOVIES")) { fprintf(stderr, "[kz-sail] KZ_NO_MOVIES: the movies are skipped\n"); return; }
        static const char* names[5] = { "avutil", "avcodec", "avformat", "swscale", "swresample" };
        const int majors[5] = { LIBAVUTIL_VERSION_MAJOR, LIBAVCODEC_VERSION_MAJOR, LIBAVFORMAT_VERSION_MAJOR,
                                LIBSWSCALE_VERSION_MAJOR, LIBSWRESAMPLE_VERSION_MAJOR };
        void* lib[5];
        for (int i = 0; i < 5; i++) {
#ifdef _WIN32
            const std::string n = std::string(names[i]) + "-" + std::to_string(majors[i]) + ".dll";
#else
            const std::string n = "lib" + std::string(names[i]) + ".so." + std::to_string(majors[i]);
#endif
            if (!(lib[i] = kz_ff_open(n))) {
                fprintf(stderr, "[kz-sail] FFmpeg not found (%s): the movies are skipped\n", n.c_str());
                return;
            }
        }
#define KZ_FF_GET(l, fn) \
        if (!(kz_ff.fn = (decltype(kz_ff.fn))kz_ff_sym(lib[l], #fn))) { \
            fprintf(stderr, "[kz-sail] FFmpeg has no " #fn ": the movies are skipped\n"); return; }
        KZ_FF_FNS(KZ_FF_GET)
#undef KZ_FF_GET
        ok = true;
    });
    return ok;
}
#endif

/* from here on, the calls go through the loaded pointers */
#define av_frame_alloc kz_ff.av_frame_alloc
#define av_frame_free kz_ff.av_frame_free
#define av_packet_alloc kz_ff.av_packet_alloc
#define av_packet_free kz_ff.av_packet_free
#define av_packet_unref kz_ff.av_packet_unref
#define avcodec_find_decoder kz_ff.avcodec_find_decoder
#define avcodec_alloc_context3 kz_ff.avcodec_alloc_context3
#define avcodec_free_context kz_ff.avcodec_free_context
#define avcodec_parameters_to_context kz_ff.avcodec_parameters_to_context
#define avcodec_open2 kz_ff.avcodec_open2
#define avcodec_flush_buffers kz_ff.avcodec_flush_buffers
#define avcodec_send_packet kz_ff.avcodec_send_packet
#define avcodec_receive_frame kz_ff.avcodec_receive_frame
#define avformat_open_input kz_ff.avformat_open_input
#define avformat_find_stream_info kz_ff.avformat_find_stream_info
#define avformat_close_input kz_ff.avformat_close_input
#define av_read_frame kz_ff.av_read_frame
#define av_seek_frame kz_ff.av_seek_frame
#define av_find_best_stream kz_ff.av_find_best_stream
#define sws_getContext kz_ff.sws_getContext
#define sws_scale kz_ff.sws_scale
#define sws_freeContext kz_ff.sws_freeContext
#define swr_alloc_set_opts2 kz_ff.swr_alloc_set_opts2
#define swr_init kz_ff.swr_init
#define swr_convert kz_ff.swr_convert
#define swr_get_out_samples kz_ff.swr_get_out_samples
#define swr_free kz_ff.swr_free
#endif
