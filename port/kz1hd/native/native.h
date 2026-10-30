/*
 * Native replacements for hot lifted SPU functions (the .c files here).
 *
 * A job module's inner loop that dominates the frame is reimplemented here in
 * host SIMD. lift.sh passes these functions' names to the SPU lifter
 * (SPU_LIFT_NATIVE), which then emits the lifted body as <name>_lifted and
 * leaves <name> to this directory.
 *
 * Exactness: the lifted loops are software-pipelined, so besides the memory a
 * loop writes, the registers it leaves behind feed the code after it. A
 * native loop therefore computes the memory itself and gets the registers by
 * replaying the last few iterations through the lifted body (KZ_REPLAY): with
 * the pointer and counter registers advanced, every carried value is
 * recomputed from local store within the pipeline depth.
 *
 * KZ_NATIVE_VERIFY=1 runs the lifted body on a copy of the context as well and
 * reports (and corrects) any difference in the registers or the local store.
 */
#ifndef KZ_NATIVE_H
#define KZ_NATIVE_H
#include "spu_context.h"
#include "spu_helpers.h"
#include <immintrin.h>

typedef void (*kz_spu_fn)(spu_context*);

/* Runs `native` (or, under KZ_NATIVE_VERIFY, both and compares). */
void kz_native_run(spu_context* ctx, kz_spu_fn native, kz_spu_fn lifted, const char* name);
/* The same for a function that returns to its caller: only the registers in
 * `live` (bit r of live[r / 32]) are defined after it -- the SPU ABI's
 * callee-saved ones and the results -- so only those are compared. */
void kz_native_run_live(spu_context* ctx, kz_spu_fn native, kz_spu_fn lifted, const char* name,
                        const uint32_t live[4]);

/* Adds `d` to all four lanes of register r (pointers and counters advance per lane). */
static inline void kz_lanes_add(spu_context* ctx, int r, uint32_t d)
{
    for (int l = 0; l < 4; l++) ctx->gpr[r]._u32[l] += d;
}
#endif
