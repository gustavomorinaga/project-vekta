/*
 * FIOS's zlib decompressor, native. See native_ppu.h.
 *
 * FIOS (the game's file layer) reads its compressed archive in blocks and has
 * each one inflated by an SPU task (Edge zlib). func_003CD7D4(decompressor r3,
 * request r4) claims one of 16 slots (a bitmap at +0x2C00, taken with
 * lwarx/stwcx.), copies the 10-word request into it (slot at +0x2C04 + 0x2C *
 * i, pending word at +0x28 = 1), clears the slot's event-flag bit and starts
 * the task, returning the slot; func_003CDA54 waits on the event flag until
 * the task clears the pending word, then copies the optional tail and frees
 * the slot. The reads that wait on this stall the main thread (~19% of its
 * time in Level 1, at ~0.7 ms an SPU-inflated 64 KB block).
 *
 * Request: [3] source (a zlib stream), [4] its size, [5] destination, [6] the
 * bytes to produce, [7] [8] [9] a copy the wait makes afterwards. The native
 * submit takes the slot the same way, inflates into the destination at once
 * and leaves the slot not pending, so the wait returns without waiting. Any
 * other shape of request (the partial form, with no destination), a stream
 * that does not inflate to exactly the bytes asked for, or memory that does
 * not take plain stores runs the lifted body, which starts the SPU task.
 *
 * KZ_FIOS_SPU=1 sends every request to the SPU as before. KZ_NATIVE_VERIFY=1:
 * every request goes to the SPU as before, the native
 * result is kept aside and compared with the SPU's in the wait.
 */
#include "native_ppu.h"
#include <zlib.h>
#include <stdlib.h>

extern "C" int vm_block_plain(uint32_t ea, uint32_t n);

namespace {

/* Raw inflate of a zlib stream (the 2-byte header skipped; the trailer is not
 * checked, as the SPU task does not either). Exactly `want` bytes or false. */
bool inflate_exact(const uint8_t* src, uint32_t src_len, uint8_t* dst, uint32_t want)
{
    if (src_len < 2 || (src[0] & 0x0F) != 8 || ((src[0] << 8) | src[1]) % 31 || (src[1] & 0x20)) return false;
    z_stream z = {};
    if (inflateInit2(&z, -15) != Z_OK) return false;
    z.next_in = const_cast<uint8_t*>(src + 2);
    z.avail_in = src_len - 2;
    z.next_out = dst;
    z.avail_out = want;
    const int rc = inflate(&z, Z_FINISH);
    const bool ok = (rc == Z_STREAM_END || (rc == Z_BUF_ERROR && z.avail_out == 0)) && z.total_out == want;
    inflateEnd(&z);
    return ok;
}

/* Takes a free slot as the guest does (lowest clear bit from the start, the
 * bitmap updated with a compare-and-swap on the big-endian word). -1: full. */
int take_slot(uint32_t bitmap_ea)
{
    uint32_t* w = (uint32_t*)(vm_base + bitmap_ea);
    for (;;) {
        const uint32_t cur_be = __atomic_load_n(w, __ATOMIC_ACQUIRE), cur = __builtin_bswap32(cur_be);
        if (cur == 0xFFFF) return -1;
        int i = 0;
        while (cur & (1u << i)) i = (i + 1) & 0xF;
        const uint32_t next_be = __builtin_bswap32(cur | (1u << i));
        uint32_t expect = cur_be;
        if (__atomic_compare_exchange_n(w, &expect, next_be, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return i;
    }
}

struct Req { uint32_t w[10]; };

bool plain_request(const Req& r)
{
    const uint32_t src = r.w[3], n = r.w[4], dst = r.w[5], want = r.w[6];
    /* the source may lie inside the destination (in place, at its tail): the
     * inflate goes to a buffer first, as the SPU has the whole input in its
     * local store before it writes anything */
    return dst && want && src && n && (uint64_t)src + n <= 0xE0000000ull && (uint64_t)dst + want <= 0xE0000000ull &&
           !(r.w[7] && !r.w[5]);
}

/* verify mode: the native result of each slot, compared in the wait */
uint8_t* s_side[16];
uint32_t s_side_len[16], s_side_dst[16];
bool s_side_ok[16];

}  // namespace

void func_003CD7D4(ppu_context* ctx)
{
    const uint32_t obj = (uint32_t)ctx->gpr[3], req = (uint32_t)ctx->gpr[4];
    Req r;
    for (int i = 0; i < 10; i++) r.w[i] = vm_read32(req + 4 * i);
    const bool active = vm_read8(obj + 0x2ED8) != 0;   /* piVar7[0xbb6] */
    if (__builtin_expect(kz_native_verify, 0)) {
        func_003CD7D4_lifted(ctx);
        const uint32_t slot = (uint32_t)ctx->gpr[3];
        const int i = slot ? (int)((slot - (obj + 0x2C04)) / 0x2C) : -1;
        if (i < 0 || i > 15 || !active || !plain_request(r)) { kz_native_fallback("func_003CD7D4"); return; }
        if (s_side_len[i] < r.w[6]) { free(s_side[i]); s_side[i] = (uint8_t*)malloc(r.w[6]); s_side_len[i] = r.w[6]; }
        s_side_dst[i] = r.w[5];
        s_side_ok[i] = s_side[i] && inflate_exact(vm_base + r.w[3], r.w[4], s_side[i], r.w[6]);
        return;
    }
    static const bool spu = getenv("KZ_FIOS_SPU") != nullptr;   /* every block on the SPU, as before */
    if (spu || !active || !plain_request(r) || !vm_block_plain(r.w[5], r.w[6])) { func_003CD7D4_lifted(ctx); return; }
    static uint8_t* tmp; static uint32_t tmp_len;
    if (tmp_len < r.w[6]) { free(tmp); tmp = (uint8_t*)malloc(r.w[6]); tmp_len = tmp ? r.w[6] : 0; }
    if (!tmp || !inflate_exact(vm_base + r.w[3], r.w[4], tmp, r.w[6])) { func_003CD7D4_lifted(ctx); return; }
    const int i = take_slot(obj + 0x2C00);
    if (i < 0) { func_003CD7D4_lifted(ctx); return; }   /* the lifted body spins for a slot as the guest does */
    memcpy(vm_base + r.w[5], tmp, r.w[6]);
    const uint32_t slot = obj + 0x2C04 + 0x2C * (uint32_t)i;
    for (int k = 0; k < 10; k++) vm_write32(slot + 4 * k, r.w[k]);
    vm_write32(slot + 0x28, 0);   /* done: the wait finds nothing pending */
    ctx->gpr[3] = slot;
}

void func_003CDA54(ppu_context* ctx)
{
    if (__builtin_expect(!kz_native_verify, 1)) { func_003CDA54_lifted(ctx); return; }
    const uint32_t obj = (uint32_t)ctx->gpr[3], slot = (uint32_t)ctx->gpr[4];
    const int i = slot ? (int)((slot - (obj + 0x2C04)) / 0x2C) : -1;
    const uint32_t want = slot ? vm_read32(slot + 0x18) : 0;
    func_003CDA54_lifted(ctx);
    if (i < 0 || i > 15 || !s_side_ok[i] || s_side_len[i] < want) return;
    kz_native_check("func_003CD7D4 inflate", s_side[i], vm_base + s_side_dst[i], want);
    s_side_ok[i] = false;
}
