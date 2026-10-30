/*
 * PSGL: vertex program upload, native. See native_ppu.h.
 *
 * func_00312218(program r3) runs before a draw that changed the vertex program
 * or its constants. Into the GCM command buffer of the current context it
 * writes, in order:
 *   - SET_TRANSFORM_PROGRAM_LOAD (0x1E9C) with the program's slot twice;
 *   - the microcode, 4 words an instruction, as SET_TRANSFORM_PROGRAM (0x0B80)
 *     bursts of 32 words and one burst for the rest;
 *   - 0x1FF0 (the program's input mask), 0x1EF8 (0x20FFFF, or 0x30FFFF for
 *     more than 32 temporaries), 0x1478 and 0x1FF4 (output masks; the latter
 *     ANDed with the context's, and the program's kept at context +0x164);
 *   - for each of its uniform parameters that is set, SET_TRANSFORM_CONSTANT_LOAD
 *     (0x1EFC) at its register with the value: a 4x4 or 3x3 matrix transposed
 *     into rows (3x3 rows end in 0), or 4 words as they are.
 * Every block first checks for room and calls the buffer's callback when there
 * is none. The native path runs only when the whole sequence fits in the room
 * already there, the buffer reservation at the start needs no wrap, and the
 * memory takes plain stores; otherwise the lifted body runs (nothing has been
 * written by then).
 *
 * Program object: +0x42 u16 instruction count, +0x44 input mask, +0x48 load
 * slot, +0x4C temporaries, +0x50 / +0x54 output masks, +0x5C microcode, and the
 * parameter tables at +0x6C, +0x78, +0x84 (count), +0x88, +0x98 (values).
 * The matrices go through lfs/stfs in the guest code; the copy keeps that
 * (a signalling NaN comes out quieted, as the lifted body leaves it).
 */
#include "native_ppu.h"

extern "C" int vm_block_plain(uint32_t ea, uint32_t n);

namespace {

enum Kind : uint8_t { K_SKIP, K_MAT4, K_MAT3, K_VEC4, K_UNKNOWN };
Kind s_kind[0x6E];
uint32_t s_table;   // the jump table the kinds were read from

/* The guest's handler for each parameter type (0x401 + i), from its jump table. */
bool load_kinds(uint32_t table)
{
    if (s_table == table) return true;
    for (uint32_t i = 0; i < 0x6E; i++) {
        const uint32_t t = table + (uint32_t)(int32_t)vm_read32(table + 4 * i);
        s_kind[i] = t == 0x00312654u ? K_SKIP : t == 0x003128CCu ? K_MAT4
                  : (t == 0x00312B20u || t == 0x00312D2Cu) ? K_MAT3
                  : (t == 0x00312C60u || t == 0x00312D00u) ? K_VEC4 : K_UNKNOWN;
    }
    s_table = table;
    return true;
}

inline uint32_t be(uint32_t v) { return __builtin_bswap32(v); }
inline void put(uint8_t*& w, uint32_t v) { const uint32_t b = be(v); memcpy(w, &b, 4); w += 4; }
inline void copy_raw(uint8_t*& w, uint32_t ea, uint32_t words) { memcpy(w, vm_base + ea, 4 * words); w += 4 * words; }
/* lfs + stfs: the word through a double and back */
inline uint32_t via_fpr(uint32_t ea)
{
    const uint32_t u = vm_read32(ea);
    float f;
    memcpy(&f, &u, 4);
    const float g = (float)(double)f;
    uint32_t r;
    memcpy(&r, &g, 4);
    return r;
}

struct Param { Kind kind; uint32_t reg, data; };

/* Returns false (and writes nothing) when the lifted body has to run. */
bool upload(ppu_context* ctx, uint32_t prog, uint32_t* written_lo, uint32_t* written_hi)
{
    const uint32_t toc = (uint32_t)ctx->gpr[2];
    const uint32_t gc = vm_read32(toc - 0x7CF4), pc = vm_read32(toc - 0x7CF0);
    if (gc != pc + 0x14) return false;
    const uint32_t n = vm_read16(prog + 0x42);
    const uint32_t cur = vm_read32(pc + 0x1C), end = vm_read32(pc + 0x18);
    if (cur + 4u * (5u * n + 7u) + 0x1000u > end) return false;    // func_003173F0 would wrap

    const uint32_t chunks = n >> 3, rest = (n & 7) * 4;
    uint32_t size = 4u * (33u * chunks + (rest ? rest + 1 : 0)) + 0x1C + 16;
    Param params[64];
    uint32_t np = 0;
    const int32_t count = (int32_t)vm_read32(prog + 0x84);
    if (count > 0) {
        if (!load_kinds(vm_read32(toc - 0x7CEC))) return false;
        const uint32_t idx = vm_read32(prog + 0x88), ent = vm_read32(prog + 0x78);
        const uint32_t names = vm_read32(prog + 0x6C), vals = vm_read32(prog + 0x98);
        for (int32_t i = 0; i < count; i++) {
            const uint32_t ip = idx + 4u * (uint32_t)i;
            const uint32_t e = ent + 8u * vm_read16(ip);
            if ((vm_read16(e + 6) & 0x13) != 0x12) continue;
            const uint32_t q = names + vm_read16(e + 4);
            const uint32_t reg = vm_read16(q + 2);
            if (reg == 0xFFFF) continue;
            const uint32_t t = (vm_read16(q) - 0x401u) & 0xFFFF;
            if (t > 0x6D) continue;
            const Kind k = s_kind[t];
            if (k == K_SKIP) continue;
            if (k == K_UNKNOWN || np == 64) return false;
            params[np++] = { k, reg, vals + ((4u * vm_read16(ip + 2)) & 0x3FFFC) };
            size += k == K_MAT4 ? 0x48 : k == K_MAT3 ? 0x38 : 0x18;
        }
    }
    if ((uint64_t)cur + size > end || !vm_block_plain(cur, size) ||
        !vm_block_plain(pc + 0x164, 4) || !vm_block_plain(pc + 0x1C, 4)) return false;

    uint8_t* w = vm_base + cur;
    const uint32_t slot = vm_read32(prog + 0x48) & 0xFFFF;
    put(w, 0x00081E9C); put(w, slot); put(w, slot);
    uint32_t src = vm_read32(prog + 0x5C);
    for (uint32_t c = 0; c < chunks; c++, src += 0x80) { put(w, 0x00800B80); copy_raw(w, src, 32); }
    if (rest) { put(w, (rest << 18) | 0xB80); copy_raw(w, src, rest); }
    put(w, 0x00041FF0); put(w, vm_read32(prog + 0x44) & 0xFFFF);
    put(w, 0x00041EF8); put(w, (vm_read32(prog + 0x4C) & 0xFF) > 0x20 ? 0x30FFFF : 0x20FFFF);
    put(w, 0x00041478); put(w, vm_read32(prog + 0x54));
    const uint32_t out = vm_read32(prog + 0x50);
    const uint32_t mask = vm_read32(pc + 0x168) & vm_read32(pc + 0x16C) & (out | vm_read32(pc + 0x174)) &
                          vm_read32(pc + 0x170);
    vm_write32(pc + 0x164, out);
    put(w, 0x00041FF4); put(w, mask);
    for (uint32_t i = 0; i < np; i++) {
        const Param& p = params[i];
        if (p.kind == K_VEC4) {
            put(w, 0x00141EFC); put(w, p.reg); copy_raw(w, p.data, 4);
        } else if (p.kind == K_MAT4) {
            put(w, 0x00441EFC); put(w, p.reg);
            for (uint32_t r = 0; r < 4; r++)
                for (uint32_t c = 0; c < 4; c++) put(w, via_fpr(p.data + 4 * (4 * c + r)));
        } else {
            put(w, 0x00341EFC); put(w, p.reg);
            for (uint32_t r = 0; r < 3; r++) {
                for (uint32_t c = 0; c < 3; c++) put(w, via_fpr(p.data + 4 * (3 * c + r)));
                put(w, 0);
            }
        }
    }
    vm_write32(pc + 0x1C, cur + size);
    ctx->gpr[3] = vm_read32(vm_read32(toc - 0x7CB0) + 0x1C);   // what func_003173F0 returned
    *written_lo = cur;
    *written_hi = cur + size;
    return true;
}

}  // namespace

void func_00312218(ppu_context* ctx)
{
    const uint32_t prog = (uint32_t)ctx->gpr[3];
    uint32_t lo, hi;
    if (__builtin_expect(!kz_native_verify, 1)) {
        if (!upload(ctx, prog, &lo, &hi)) func_00312218_lifted(ctx);
        return;
    }
    /* Verify: the native block and the two context words, then the lifted body
     * over the same memory; the lifted result stays. */
    const uint32_t toc = (uint32_t)ctx->gpr[2], pc = vm_read32(toc - 0x7CF0);
    const uint32_t cur0 = vm_read32(pc + 0x1C), m0 = vm_read32(pc + 0x164);
    static uint8_t mine[0x10000];
    const uint64_t r3 = ctx->gpr[3];
    if (vm_read32(pc + 0x18) - cur0 > sizeof mine || !upload(ctx, prog, &lo, &hi)) {
        kz_native_fallback("func_00312218 commands");
        func_00312218_lifted(ctx);
        return;
    }
    const uint32_t n = hi - lo, cur1 = vm_read32(pc + 0x1C), m1 = vm_read32(pc + 0x164);
    memcpy(mine, vm_base + lo, n);
    memset(vm_base + lo, 0xCD, n);
    vm_write32(pc + 0x1C, cur0);
    vm_write32(pc + 0x164, m0);
    ctx->gpr[3] = r3;
    func_00312218_lifted(ctx);
    kz_native_check("func_00312218 commands", mine, vm_base + lo, n);
    const uint32_t ctxw[2] = { cur1, m1 }, ctxl[2] = { vm_read32(pc + 0x1C), vm_read32(pc + 0x164) };
    kz_native_check("func_00312218 context", ctxw, ctxl, sizeof ctxw);
}
