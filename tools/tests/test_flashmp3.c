/* The native replacement of the firmware's MP3 decoder (port/kz1hd/native/flashmp3.c), on two MP3 files made for
 * this test from a tone and noise (tools/tests/data/flashmp3_*.l3, LAME through ffmpeg; nothing of the game):
 *
 * 1. The call as MultiStream makes it (the game's LS addresses): bytes consumed = the frame's length, PCM bytes
 *    0x900 (MPEG-1) or 0x480 (MPEG-2), -1 for an MPEG-1 stream's first frame, big-endian samples at r6 + 0x900,
 *    which is what it returns, and the rest of the work space cleared.
 * 2. The state lives in the buffers: two streams decoded call by call through the same LS, with only their state
 *    blocks kept between calls (as MultiStream saves r9..r11 and r8/r10) and the rest of LS overwritten, give the
 *    same samples as each stream alone.
 * 3. The samples: against minimp3 decoding each file on its own, more than 60 dB SNR from the third frame on.
 * 4. The stand-in flashMP3.pic: what the game's loader (func_0037DE70) takes from it is what it took from the firmware
 *    file (load range 0x100.., 0x10360 bytes at LS offset 0x80, entry 0x74A0, bss at 0x10090 for 0x3D0), and every
 *    byte it copies is zero.
 *
 * Built by test_flashmp3.sh. Exit 0 on pass, 1 on a failure. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spu_context.h"
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* ctx) { (void)ctx; }

typedef struct { uint32_t in, a, b, heap, ov[2], syn[2], res; } kz_mp3_args;
uint32_t kz_mp3_decode(uint8_t* ls, const kz_mp3_args* x);
size_t kz_flashmp3_standin(uint8_t* buf, size_t size);

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_fail = 1; } } while (0)

static const kz_mp3_args GAME = { 0x32900, 0x3F980, 0x3F990, 0x2C900, { 0x34980, 0x35D00 }, { 0x34100, 0x35480 }, 0x35280 };

typedef struct { uint8_t* data; long n; long off[2048]; int len[2048]; int frames; } mp3file;

static int flen(const uint8_t* p)
{
    static const int kb[2][15] = { { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 },
                                   { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } };
    static const int hz[3] = { 44100, 48000, 32000 };
    const int m1 = (p[1] >> 3) & 1, bi = p[2] >> 4, si = (p[2] >> 2) & 3;
    return (m1 ? 144 : 72) * 1000 * kb[m1][bi] / (hz[si] >> (m1 ? 0 : 1)) + ((p[2] >> 1) & 1);
}

static void load(const char* path, mp3file* f)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) { printf("FAIL: no %s\n", path); exit(1); }
    fseek(fp, 0, SEEK_END); f->n = ftell(fp); fseek(fp, 0, SEEK_SET);
    f->data = malloc((size_t)f->n);
    if (fread(f->data, 1, (size_t)f->n, fp) != (size_t)f->n) exit(1);
    fclose(fp);
    f->frames = 0;
    for (long o = 0; o + 4 <= f->n && f->frames < 2048;) {
        if (f->data[o] != 0xFF || (f->data[o + 1] & 0xE0) != 0xE0) { printf("FAIL: %s: no frame at %ld\n", path, o); exit(1); }
        f->off[f->frames] = o; f->len[f->frames] = flen(f->data + o); o += f->len[f->frames++];
    }
}

/* One call: the frame into r3's place, the decoder, the PCM out (n samples). Returns the r5 word. */
static int32_t call(uint8_t* ls, const mp3file* f, int i, int16_t* pcm, uint32_t* consumed)
{
    memcpy(ls + GAME.in, f->data + f->off[i], (size_t)f->len[i]);
    const uint32_t out = kz_mp3_decode(ls, &GAME);
    CHECK(out == GAME.heap + 0x900, "frame %d: the PCM is returned at r6 + 0x900 (got 0x%05X)", i, out);
    *consumed = (uint32_t)ls[GAME.a] << 24 | ls[GAME.a + 1] << 16 | ls[GAME.a + 2] << 8 | ls[GAME.a + 3];
    const int32_t b = (int32_t)((uint32_t)ls[GAME.b] << 24 | ls[GAME.b + 1] << 16 | ls[GAME.b + 2] << 8 | ls[GAME.b + 3]);
    for (int k = 0; b > 0 && k < b / 2; k++) pcm[k] = (int16_t)(ls[out + 2 * k] << 8 | ls[out + 2 * k + 1]);
    return b;
}

static const uint32_t STATE[3][2] = { { 0x34100, 0x35480 }, { 0x35480, 0x35D00 }, { 0x35D00, 0x36600 } };   /* r9..r11, r10, r8 */

static void contract_and_alone(const mp3file* f, int mpeg1, int16_t* alone)
{
    uint8_t* ls = calloc(1, 0x40000);
    const int per = mpeg1 ? 1152 : 576;
    for (int i = 0; i < f->frames; i++) {
        uint32_t used; int16_t* pcm = alone + (long)per * i;
        memset(ls + GAME.heap, 0x5A, 0x5800);   /* the work space as another call left it */
        const int32_t b = call(ls, f, i, pcm, &used);
        CHECK(used == (uint32_t)f->len[i], "frame %d: consumed %u, the frame is %d", i, used, f->len[i]);
        if (i == 0 && mpeg1) CHECK(b == -1, "an MPEG-1 stream's first frame gives -1 (got %d)", b);
        else CHECK(b == per * 2, "frame %d: %d PCM bytes, %d expected", i, b, per * 2);
        if (b < 0) memset(pcm, 0, (size_t)per * 2);
        int dirty = 0;
        for (uint32_t o = GAME.heap; o < GAME.heap + 0x900; o++) dirty |= ls[o] == 0x5A;
        CHECK(!dirty, "frame %d: the work space is cleared", i);
    }
    free(ls);
}

static double snr(const int16_t* ref, const int16_t* got, long n)
{
    double s = 0, e = 0;
    for (long i = 0; i < n; i++) { s += (double)ref[i] * ref[i]; e += (double)(ref[i] - got[i]) * (ref[i] - got[i]); }
    return e > 0 ? 10 * log10(s / e) : 999;
}

#define MINIMP3_ONLY_MP3
#include "minimp3.h"   /* the declarations; the implementation is in flashmp3.c */

int main(int argc, char** argv)
{
    if (argc != 3) { printf("usage: %s <mpeg1.mp3> <mpeg2.mp3>\n", argv[0]); return 1; }
    mp3file f1, f2;
    load(argv[1], &f1); load(argv[2], &f2);
    CHECK(f1.frames > 20 && f2.frames > 20 && (f1.data[1] & 0x08) && !(f2.data[1] & 0x08), "the fixtures: MPEG-1 and MPEG-2");
    int16_t* a1 = calloc((size_t)f1.frames, 1152 * 2); int16_t* a2 = calloc((size_t)f2.frames, 576 * 2);
    contract_and_alone(&f1, 1, a1);
    contract_and_alone(&f2, 0, a2);
    printf("contract: %d MPEG-1 and %d MPEG-2 frames\n", f1.frames, f2.frames);

    /* 2. interleaved through one LS, state blocks kept apart, everything else trashed between calls */
    uint8_t* ls = calloc(1, 0x40000);
    uint8_t* keep[2] = { calloc(1, 0x40000), calloc(1, 0x40000) };
    int16_t pcm[1152]; int same = 1;
    for (int i = 0; i < f1.frames || i < f2.frames; i++)
        for (int s = 0; s < 2; s++) {
            const mp3file* f = s ? &f2 : &f1;
            if (i >= f->frames) continue;
            memset(ls, 0xA5, 0x40000);
            for (int r = 0; r < 3; r++) memcpy(ls + STATE[r][0], keep[s] + STATE[r][0], STATE[r][1] - STATE[r][0]);
            uint32_t used; const int32_t b = call(ls, f, i, pcm, &used);
            for (int r = 0; r < 3; r++) memcpy(keep[s] + STATE[r][0], ls + STATE[r][0], STATE[r][1] - STATE[r][0]);
            const int16_t* ref = s ? a2 + 576L * i : a1 + 1152L * i;
            if (b > 0 && memcmp(pcm, ref, (size_t)b)) same = 0;
        }
    CHECK(same, "two streams through one LS, each with its own state block, decode as each alone");
    printf("state: two interleaved streams decode as each alone\n");

    /* 3. against minimp3 on its own (a fresh decoder over the whole file) */
    for (int s = 0; s < 2; s++) {
        const mp3file* f = s ? &f2 : &f1; const int per = s ? 576 : 1152;
        int16_t* ref = calloc((size_t)f->frames, (size_t)per * 2);
        mp3dec_t d; mp3dec_init(&d); mp3dec_frame_info_t info; long o = 0; int i = 0;
        while (o < f->n && i < f->frames) {
            const int n = mp3dec_decode_frame(&d, f->data + o, (int)(f->n - o), ref + (long)per * i, &info);
            if (!info.frame_bytes) break;
            o += info.frame_bytes; if (n) i++;
        }
        const double db = snr(ref + per * 2L, (s ? a2 : a1) + per * 2L, (long)per * (i - 2));
        CHECK(db > 60, "MPEG-%d: %.1f dB against minimp3 alone", s ? 2 : 1, db);
        printf("samples: MPEG-%d %.1f dB against minimp3 alone over %d frames\n", s ? 2 : 1, db, i - 2);
        free(ref);
    }
    /* 4. the stand-in, read as func_0037DE70 reads it */
    {
        static uint8_t f[0x20000];
        const size_t n = kz_flashmp3_standin(f, sizeof f);
        #define B32(o) ((uint32_t)f[o] << 24 | (uint32_t)f[(o) + 1] << 16 | (uint32_t)f[(o) + 2] << 8 | f[(o) + 3])
        #define B16(o) ((uint32_t)f[o] << 8 | f[(o) + 1])
        CHECK(n > 0 && f[1] == 'E' && f[2] == 'L' && B16(0x12) == 0x17, "the loader's checks: 'EL', e_machine SPU");
        uint32_t lo = 0xFFFFFFFF, hi = 0x10, addr = 0xFFFFFFFF, bss_off = 0, bss_size = 0;
        const uint32_t sh = B32(0x20);
        for (uint32_t i = 0; i < B16(0x30); i++) {
            const uint32_t s = sh + 0x28 * i, type = B32(s + 4), flags = B32(s + 8), off = B32(s + 0x10), sz = B32(s + 0x14);
            if (!(flags & 2) || !sz) continue;
            if (off < lo) lo = off;
            if (type & 8) { bss_off = off; bss_size = sz; }
            if (off + sz > hi) hi = off + sz;
            if (B32(s + 0xC) < addr) addr = B32(s + 0xC);
        }
        const uint32_t size = (hi - lo + 15) & ~15u;
        CHECK(lo == 0x100 && size == 0x10360 && addr == 0x80 && B32(0x18) == 0x74A0 && bss_off == 0x10090 && bss_size == 0x3D0,
              "the stand-in gives the firmware file's layout: offset 0x%X size 0x%X addr 0x%X entry 0x%X bss 0x%X/0x%X",
              lo, size, addr, B32(0x18), bss_off, bss_size);
        int zero = 1;
        for (uint32_t o = lo; o < lo + size && o < n; o++) if (o < 0x10460 && f[o]) zero = 0;
        CHECK(zero, "everything the loader copies is zero: no code");
        printf("stand-in: %zu bytes, the firmware file's layout, no code\n", n);
    }
    printf(g_fail ? "FAILED\n" : "PASS\n");
    return g_fail;
}
