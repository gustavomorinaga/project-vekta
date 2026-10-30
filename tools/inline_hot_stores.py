#!/usr/bin/env python3
"""Inline the guest stores of the hottest lifted functions.

Usage: inline_hot_stores.py <gen dir> <list file>

The list holds one function name (func_XXXXXXXX) per line, the ones whose
stores cost the most in a profile (prof_report.py --callers vm_write32 and
friends). Inside those bodies vm_write8/16/32/64 become vm_write*_hot: the store
window test, the lock-line bitmap and the store inline, the call only when the
window misses. Inlining every store in the lift costs more than it saves (+6 MB
of code); a few dozen functions carry most of them.

Run after ppu_regcache.py (lift.sh does), then pgo.sh. Idempotent.
"""
import re
import sys
from pathlib import Path

HELPERS = r'''
/* inline_hot_stores.py: the stores of the hottest functions, inline. The same
 * test as VM_STORE_WINDOW in ppu_loader.cpp; a miss takes the full store. */
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t g_st_span;
extern unsigned char g_spu_coh_bitmap[];
#ifdef __cplusplus
}
#endif
#define VM_WRITE_HOT(bits, T, swap)                                             \
static inline __attribute__((always_inline)) void vm_write##bits##_hot(uint64_t a, T v) \
{                                                                              \
    const uint32_t ea = (uint32_t)a, line = ea >> 7;                           \
    if (__builtin_expect(ea - 0x1000u < g_st_span &&                           \
                         !((g_spu_coh_bitmap[line >> 3] >> (line & 7)) & 1u), 1)) { \
        const T raw = swap(v); memcpy(vm_base + ea, &raw, sizeof raw); return; } \
    vm_write##bits(a, v);                                                      \
}
static inline uint8_t vm_hot_id8(uint8_t v) { return v; }
VM_WRITE_HOT(8, uint8_t, vm_hot_id8)
VM_WRITE_HOT(16, uint16_t, __builtin_bswap16)
VM_WRITE_HOT(32, uint32_t, __builtin_bswap32)
VM_WRITE_HOT(64, uint64_t, __builtin_bswap64)
'''
MARK = "/* inline_hot_stores.py:"


def main():
    gen, lst = Path(sys.argv[1]), Path(sys.argv[2])
    hot = {l.split()[-1] for l in lst.read_text().splitlines() if l.strip()}
    hdr = gen / "ppu_recomp.h"
    h = hdr.read_bytes().decode()
    if MARK not in h:
        anchor = "#ifndef PPU_NO_INLINE_LOADS"
        assert anchor in h, "ppu_recomp.h: no anchor for the helpers"
        h = h.replace(anchor, HELPERS.lstrip("\n") + anchor, 1)
        hdr.write_bytes(h.encode())
    start = re.compile(r"^void (func_[0-9A-F]{8})\(ppu_context\* ctx\) \{$")
    store = re.compile(r"\bvm_write(8|16|32|64)\(")
    done = 0
    for f in sorted(gen.glob("ppu_recomp_*.cpp")):
        lines = f.read_bytes().decode().split("\n")
        inside = False
        changed = False
        for i, l in enumerate(lines):
            m = start.match(l)
            if m:
                inside = m.group(1) in hot
                done += inside
            elif l == "}":
                inside = False
            elif inside and "vm_write" in l:
                n = store.sub(r"vm_write\1_hot(", l)
                if n != l:
                    lines[i] = n
                    changed = True
        if changed:
            f.write_bytes("\n".join(lines).encode())
    print(f"inline_hot_stores: {done} of {len(hot)} functions")


if __name__ == "__main__":
    main()
