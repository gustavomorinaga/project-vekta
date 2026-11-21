/*
 * The EBOOT check: is the EBOOT.elf the game is about to load the one this build was lifted from? tools/kzbuild.py's
 * lift writes work/lift/gen/eboot_id.h (KZ_EBOOT_SHA256, KZ_EBOOT_SIZE) from the file it lifts; main.cpp calls
 * kz_eboot_check() on argv[1] before ppu_load_elf. Another EBOOT (another region or version, a file replaced after
 * the build) would otherwise run lifted code that does not match its data. The size is compared first, then the
 * SHA-256 (about 20 MB: tens of milliseconds). The file is opened with fopen, as ppu_load_elf opens it, so any path
 * the loader can open, this can too. Nothing of the EBOOT is embedded in the build: only its size and hash.
 * SHA-256 written here from FIPS 180-4 (after Project Helghast's port/kz3/kz3_embed.h, undecuple, MIT, for the idea).
 * Header only, plain C: tools/tests/test_eboot_check.c checks it on Linux and under Wine.
 */
#ifndef KZ_EBOOT_CHECK_H
#define KZ_EBOOT_CHECK_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;     /* bytes hashed */
    uint8_t buf[64];
    size_t fill;
} kz_sha256;

static const uint32_t KZ_SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define KZ_ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static inline void kz_sha256_block(kz_sha256* s, const uint8_t* p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = KZ_ROR32(w[i - 15], 7) ^ KZ_ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = KZ_ROR32(w[i - 2], 17) ^ KZ_ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (KZ_ROR32(e, 6) ^ KZ_ROR32(e, 11) ^ KZ_ROR32(e, 25)) + ((e & f) ^ (~e & g)) + KZ_SHA256_K[i] + w[i];
        uint32_t t2 = (KZ_ROR32(a, 2) ^ KZ_ROR32(a, 13) ^ KZ_ROR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static inline void kz_sha256_init(kz_sha256* s)
{
    static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, H0, sizeof H0);
    s->len = 0;
    s->fill = 0;
}

static inline void kz_sha256_update(kz_sha256* s, const void* data, size_t n)
{
    const uint8_t* p = (const uint8_t*)data;
    s->len += n;
    if (s->fill) {
        size_t take = 64 - s->fill < n ? 64 - s->fill : n;
        memcpy(s->buf + s->fill, p, take);
        s->fill += take; p += take; n -= take;
        if (s->fill < 64) return;
        kz_sha256_block(s, s->buf);
        s->fill = 0;
    }
    for (; n >= 64; p += 64, n -= 64) kz_sha256_block(s, p);
    memcpy(s->buf, p, n);
    s->fill = n;
}

/* The digest as 64 lowercase hex digits and a 0. */
static inline void kz_sha256_hex(kz_sha256* s, char out[65])
{
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80, zero = 0, lenb[8];
    kz_sha256_update(s, &pad, 1);
    while (s->fill != 56) kz_sha256_update(s, &zero, 1);
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    kz_sha256_update(s, lenb, 8);
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", (unsigned)(s->h[i / 4] >> (24 - 8 * (i % 4))) & 0xff);
    out[64] = 0;
}

enum { KZ_EBOOT_EQUAL = 0, KZ_EBOOT_DIFFERENT = 1, KZ_EBOOT_UNREADABLE = 2 };

/* path against the expected SHA-256 (64 hex digits) and size. *size_out gets the file's size (-1 when unreadable),
 * hex_out its SHA-256 when the sizes match ("" otherwise). */
static inline int kz_eboot_check(const char* path, const char* want_hex, long long want_size, long long* size_out,
                                 char hex_out[65])
{
    hex_out[0] = 0;
    *size_out = -1;
    FILE* f = fopen(path, "rb");
    if (!f) return KZ_EBOOT_UNREADABLE;
    long long size = 0;
    if (fseeko(f, 0, SEEK_END) == 0) size = (long long)ftello(f);
    if (size <= 0 || fseeko(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return KZ_EBOOT_UNREADABLE;
    }
    *size_out = size;
    if (size != want_size) {
        fclose(f);
        return KZ_EBOOT_DIFFERENT;
    }
    kz_sha256 s;
    kz_sha256_init(&s);
    enum { CHUNK = 1 << 20 };
    uint8_t* buf = (uint8_t*)malloc(CHUNK);
    if (!buf) {
        fclose(f);
        return KZ_EBOOT_UNREADABLE;
    }
    size_t n;
    long long seen = 0;
    while ((n = fread(buf, 1, CHUNK, f)) > 0) {
        kz_sha256_update(&s, buf, n);
        seen += (long long)n;
    }
    int err = ferror(f);
    free(buf);
    fclose(f);
    if (err || seen != size) return KZ_EBOOT_UNREADABLE;
    kz_sha256_hex(&s, hex_out);
    return strcmp(hex_out, want_hex) == 0 ? KZ_EBOOT_EQUAL : KZ_EBOOT_DIFFERENT;
}
#endif
