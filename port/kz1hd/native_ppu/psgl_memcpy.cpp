/*
 * PSGL's memcpy, native. See native_ppu.h.
 *
 * func_0031F4C0(dst r3, src r4, n r5) copies with the widest unit both
 * pointers are aligned to -- 16 bytes (lvx/stvx), 8 or 4 -- and hands the tail
 * (or everything, unaligned) to libc's memcpy (func_003AC4E4), which returns
 * its destination. A 16-byte-aligned copy of more than 0x7F bytes goes to the
 * asynchronous copy path instead when it is enabled. The native version is one
 * host memcpy, for ranges that do not overlap and take plain stores; anything
 * else runs the lifted body.
 *
 * r3 on return is what the guest leaves there: the destination, or, when libc
 * copied a tail, the start of that tail.
 */
#include "native_ppu.h"

extern "C" int vm_block_plain(uint32_t ea, uint32_t n);

namespace {

bool copy(ppu_context* ctx)
{
    const uint32_t dst = (uint32_t)ctx->gpr[3], src = (uint32_t)ctx->gpr[4], n = (uint32_t)ctx->gpr[5];
    const uint32_t both = dst | src;
    const uint32_t unit = !(both & 15) ? 16 : !(both & 7) ? 8 : !(both & 3) ? 4 : 1;
    if (unit == 16 && n > 0x7F &&
        (int32_t)vm_read32(vm_read32((uint32_t)ctx->gpr[2] - 0x7C24)) > 0) return false;   // async copy
    if (n) {
        if ((uint64_t)dst + n > 0x100000000ull || (uint64_t)src + n > 0x100000000ull) return false;
        if (dst < src + n && src < dst + n) return false;                                  // overlap
        if (!vm_block_plain(dst, n)) return false;
        memcpy(vm_base + dst, vm_base + src, n);
    }
    const uint32_t bulk = unit == 1 ? 0 : n & ~(unit - 1);
    ctx->gpr[3] = dst + (bulk != n ? bulk : 0);
    return true;
}

}  // namespace

void func_0031F4C0(ppu_context* ctx)
{
    if (__builtin_expect(!kz_native_verify, 1)) {
        if (!copy(ctx)) func_0031F4C0_lifted(ctx);
        return;
    }
    const uint32_t dst = (uint32_t)ctx->gpr[3], n = (uint32_t)ctx->gpr[5];
    static uint8_t mine[1 << 20];
    ppu_context in = *ctx;
    if (n > sizeof mine || !copy(ctx)) {
        kz_native_fallback("func_0031F4C0");
        func_0031F4C0_lifted(ctx);
        return;
    }
    const uint64_t r3 = ctx->gpr[3];
    memcpy(mine, vm_base + dst, n);
    memset(vm_base + dst, 0xCD, n);
    *ctx = in;
    func_0031F4C0_lifted(ctx);
    kz_native_check("func_0031F4C0", mine, vm_base + dst, n);
    const uint64_t r3l = ctx->gpr[3];
    kz_native_check("func_0031F4C0 r3", &r3, &r3l, 8);
}
