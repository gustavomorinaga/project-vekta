/*
 * The MP3 decoder MultiStream (SPU image 3) calls for the game's music, without the firmware's flashMP3.pic.
 *
 * MultiStream loads /dev_flash/sys/external/flashMP3.pic into its local store at 0x1A880 and calls its entry, LS
 * 0x21D20, once per MP3 frame. What the call is, read from the .pic (its wrapper at 0x21D20 to 0x22300) and from
 * traces of the calls in the game (docs/dev/flashmp3-native.md):
 *
 *   r3  the frame, from its header (one stream's frames in order)
 *   r4  a block whose word 0 the decoder sets to the bytes it consumed (the frame's length)
 *   r5  a block whose word 0 it sets to the PCM bytes it wrote, or -1 when the frame gave none
 *   r6  0x5800 bytes of work space, cleared on entry; the PCM goes to r6 + 0x900: big-endian 16-bit samples,
 *       1152 per channel for MPEG-1 (576 for MPEG-2), channels interleaved; r3 and r5 return that address
 *   r7, r8    channel 0 and 1 overlap state     r9, r10   channel 0 and 1 synthesis state
 *   r11       the bit reservoir                  r12       a block of MultiStream's, left as it was
 *
 * The decoder keeps nothing of its own between calls: MultiStream loads other code and data over it between decode
 * batches, and saves and restores the state buffers (for a mono stream r9, r7 and r11, LS 0x34100 to 0x35480) to
 * main memory. So the state here lives in those buffers too, in minimp3's form: overlap in r7/r8, the even (and for
 * stereo the odd) synthesis entries in r9/r10, the reservoir in r11 and its byte count, the last header and a mark
 * in r7's second half. MultiStream hands over zeroed buffers when a stream starts; a buffer without the mark is
 * that. For an MPEG-1 stream the firmware answers -1 for that first frame, for an MPEG-2 one it gives the frame
 * (every traced stream start); this does the same, after decoding the frame into the state.
 *
 * The bit reservoir follows the firmware's too: it keeps the raw last 511 bytes of the stream's main data (zeros at
 * the start), where minimp3 keeps only the bytes a frame did not use. The game's encoder points main_data_begin into
 * bytes earlier frames already used; minimp3 alone refused those frames and decoded the next ones wrong.
 *
 * The decoding is minimp3's (third_party/minimp3, CC0); its samples are not bit-identical to the firmware decoder's
 * (another implementation of the same standard); the comparison is in docs/dev/flashmp3-native.md.
 */
#include <stdint.h>
#include <string.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "spu_context.h"

#define KZ_MP3_MARK   0x4B5A4D33u   /* "KZM3" */
#define HEAP_SIZE     0x5800u
#define PCM_OFFSET    0x900u
#define LS_SIZE       0x40000u

static void wr32(uint8_t* ls, uint32_t a, uint32_t v) { ls[a] = (uint8_t)(v >> 24); ls[a + 1] = (uint8_t)(v >> 16); ls[a + 2] = (uint8_t)(v >> 8); ls[a + 3] = (uint8_t)v; }

/* The length of the Layer III frame at p (with padding), 0 if p is not one or is free format. */
static int frame_len(const uint8_t* p)
{
    static const uint16_t kbps[2][15] = { { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
                                          { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } };
    static const uint16_t hz[3] = { 44100, 48000, 32000 };
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0 || ((p[1] >> 1) & 3) != 1) return 0;   /* sync, Layer III */
    const int ver = (p[1] >> 3) & 3;                 /* 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5 */
    const int bi = p[2] >> 4, si = (p[2] >> 2) & 3;
    if (ver == 1 || bi == 0 || bi == 15 || si == 3) return 0;
    const int mpeg1 = ver == 3;
    const int rate = hz[si] >> (mpeg1 ? 0 : ver == 2 ? 1 : 2);
    return (mpeg1 ? 144 : 72) * 1000 * kbps[mpeg1][bi] / rate + ((p[2] >> 1) & 1);
}

typedef struct { uint32_t in, a, b, heap, ov[2], syn[2], res; } kz_mp3_args;

static void load_state(const uint8_t* ls, const kz_mp3_args* x, mp3dec_t* d, int* fresh)
{
    const uint8_t* tail = ls + x->ov[0] + 288 * 4;   /* r7's second half */
    uint32_t mark; memcpy(&mark, tail, 4);
    if (mark != KZ_MP3_MARK) {   /* a new stream: nothing decoded, a reservoir of zeros as the firmware's */
        memset(d, 0, sizeof *d);
        d->reserv = 511;
        *fresh = 1;
        return;
    }
    *fresh = 0;
    memcpy(d->mdct_overlap[0], ls + x->ov[0], 288 * 4);
    memcpy(d->mdct_overlap[1], ls + x->ov[1], 288 * 4);
    for (int i = 0; i < 480; i++) {
        memcpy(&d->qmf_state[2 * i], ls + x->syn[0] + 4 * i, 4);
        memcpy(&d->qmf_state[2 * i + 1], ls + x->syn[1] + 4 * i, 4);
    }
    memcpy(&d->reserv, tail + 4, 4);
    memcpy(&d->free_format_bytes, tail + 8, 4);
    memcpy(d->header, tail + 12, 4);
    memcpy(d->reserv_buf, ls + x->res, 511);
    if (d->reserv < 0 || d->reserv > 511) d->reserv = 0;
}

static void save_state(uint8_t* ls, const kz_mp3_args* x, const mp3dec_t* d, int nch)
{
    uint8_t* tail = ls + x->ov[0] + 288 * 4;
    const uint32_t mark = KZ_MP3_MARK;
    memcpy(ls + x->ov[0], d->mdct_overlap[0], 288 * 4);
    for (int i = 0; i < 480; i++) memcpy(ls + x->syn[0] + 4 * i, &d->qmf_state[2 * i], 4);
    if (nch == 2) {   /* a mono stream's MultiStream saves only channel 0's buffers */
        memcpy(ls + x->ov[1], d->mdct_overlap[1], 288 * 4);
        for (int i = 0; i < 480; i++) memcpy(ls + x->syn[1] + 4 * i, &d->qmf_state[2 * i + 1], 4);
    }
    memcpy(tail, &mark, 4);
    memcpy(tail + 4, &d->reserv, 4);
    memcpy(tail + 8, &d->free_format_bytes, 4);
    memcpy(tail + 12, d->header, 4);
    memcpy(ls + x->res, d->reserv_buf, 511);
}

/* One call of the decoder on a local store image; returns the PCM address (what r3 and r5 return). */
uint32_t kz_mp3_decode(uint8_t* ls, const kz_mp3_args* x)
{
    memset(ls + x->heap, 0, HEAP_SIZE);
    wr32(ls, x->a, 0);
    wr32(ls, x->b, 0);
    const uint32_t out = x->heap + PCM_OFFSET;

    mp3dec_t d; int fresh;   /* minimp3 resets on a header that does not match the last; the given frame is taken as is */
    load_state(ls, x, &d, &fresh);
    const uint8_t* p = ls + x->in;
    int len = frame_len(p);
    const int avail = (int)(LS_SIZE - x->in);
    if (!len || len > avail) len = avail < MAX_FREE_FORMAT_FRAME_SIZE + 4 ? avail : MAX_FREE_FORMAT_FRAME_SIZE + 4;
    else memcpy(d.header, p, 4);   /* the frame as given is the frame: no search for the next one */

    uint8_t tail[511]; const int had = d.reserv;   /* the raw reservoir before this frame */
    memcpy(tail, d.reserv_buf, 511);
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info;
    int n = mp3dec_decode_frame(&d, p, len, pcm, &info);
    if (info.frame_bytes > 0 && frame_len(p)) {   /* the raw reservoir after it: the last 511 bytes of main data */
        const int mpeg1 = (p[1] >> 3) & 1, mono = (p[3] >> 6) == 3, crc = !(p[1] & 1);
        const int side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
        const int start = 4 + (crc ? 2 : 0) + side, md = info.frame_bytes - start;
        if (md > 0) {
            uint8_t cat[511 + MAX_FREE_FORMAT_FRAME_SIZE];
            memcpy(cat, tail, (size_t)had);
            memcpy(cat + had, p + start, (size_t)md);
            const int total = had + md, keep = total < 511 ? total : 511;
            memcpy(d.reserv_buf, cat + total - keep, (size_t)keep);
            d.reserv = keep;
        }
    }
    /* ASSUMPTION (from traces, not read out of the decoder): the firmware gives nothing for an MPEG-1 stream's first
     * frame (every MPEG-1 stream start traced) and a full frame for an MPEG-2 one (every MPEG-2 start traced). */
    if (fresh && (p[1] & 0x08)) n = 0;
    wr32(ls, x->a, (uint32_t)info.frame_bytes);
    if (n > 0) {
        const int total = n * info.channels;
        for (int i = 0; i < total; i++) {
            ls[out + 2 * i] = (uint8_t)((uint16_t)pcm[i] >> 8);
            ls[out + 2 * i + 1] = (uint8_t)pcm[i];
        }
        wr32(ls, x->b, (uint32_t)(total * 2));
    } else {
        wr32(ls, x->b, 0xFFFFFFFFu);
    }
    save_state(ls, x, &d, info.channels ? info.channels : 1);
    return out;
}

/* The SPU entry: LS 0x21D20 in image 3 (stubs.cpp registers it). */
void kz_flashmp3_entry(spu_context* ctx)
{
    kz_mp3_args x;
    x.in = ctx->gpr[3]._u32[0] & (LS_SIZE - 1);
    x.a = ctx->gpr[4]._u32[0] & (LS_SIZE - 1);
    x.b = ctx->gpr[5]._u32[0] & (LS_SIZE - 1);
    x.heap = ctx->gpr[6]._u32[0] & (LS_SIZE - 1);
    x.ov[0] = ctx->gpr[7]._u32[0] & (LS_SIZE - 1);
    x.ov[1] = ctx->gpr[8]._u32[0] & (LS_SIZE - 1);
    x.syn[0] = ctx->gpr[9]._u32[0] & (LS_SIZE - 1);
    x.syn[1] = ctx->gpr[10]._u32[0] & (LS_SIZE - 1);
    x.res = ctx->gpr[11]._u32[0] & (LS_SIZE - 1);
    const uint32_t out = kz_mp3_decode(ctx->ls, &x);
    for (int w = 0; w < 4; w++) { ctx->gpr[3]._u32[w] = out; ctx->gpr[5]._u32[w] = out; }
    SPU_RET(ctx);
}
