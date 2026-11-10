/*
 * KZ_NATIVE_VERIFY for native functions with wide effects (native_ppu.h):
 * run the native body and the lifted one from the same state and compare
 * every byte either of them stored, the return register and the registers the
 * ABI says a callee keeps.
 *
 * Needs a build with the guest store journal (KZ_STORE_JOURNAL=ON, ps3recomp
 * patch 3034): the journal notes each store the calling thread makes, native
 * or lifted, with the bytes it overwrote. Per verified call:
 *   1. journal on, the native body; its final bytes are kept;
 *   2. its stores undone, the registers put back;
 *   3. journal on, the lifted body (its result is the one that stays);
 *   4. each byte either side stored is compared: the native side's final value
 *      (or the original, where only the lifted side stored) against memory now.
 * Stores below the stack pointer at entry are left out: they are the callee
 * frames of a call that has returned (the native body has none of its own);
 * so are the CR and LR save slots of the caller's frame header (sp+8..sp+24),
 * which the ABI gives the callee and which are dead once it returns.
 * What other threads store meanwhile is not in the journals. With a probe (a
 * fingerprint of what the function reads), a call whose inputs changed during
 * the check (at entry, after the undo, at the second entry) is counted as not
 * compared instead of mismatched; every native body under this check should
 * give one.
 *
 * A native function called while a check runs (by either body) runs its
 * lifted body: journals do not nest, and the lifted side stays all lifted. A
 * native body that runs another native one inside a check calls its body
 * directly.
 *
 * Without the journal the native body runs alone and the check says so once.
 */
#ifndef KZ_NATIVE_VERIFY_H
#define KZ_NATIVE_VERIFY_H
#include "native_ppu.h"

/* The native body: true when it handled the call; false when it left it to the
 * lifted body without storing anything. */
typedef bool (*kz_native_body)(ppu_context* ctx);

/* KZ_NATIVE_VERIFY: verify one call (see above). Otherwise: native, or lifted
 * when the native body declines. */
/* A fingerprint of the input state a function reads (optional): taken at the
 * entry of both runs, it tells a mismatch whose inputs changed between them
 * (another thread, an undo that missed something) from one inside the call. */
typedef uint64_t (*kz_native_probe)(const ppu_context* ctx);
void kz_native_run(const char* name, ppu_context* ctx, kz_native_body native, void (*lifted)(ppu_context*),
                   kz_native_probe probe = nullptr);
/* Diagnostics: the guest words behind a probe (address list), so that a
 * mismatch whose inputs changed can say which words and whether the first
 * run's journal had them. Optional; set by the function that has a probe. */
typedef void (*kz_native_probe_words)(const ppu_context* ctx, uint32_t* ea, uint32_t* n, uint32_t cap);
extern kz_native_probe_words kz_native_probe_words_fn;

/* KZ_NATIVE_PSGL=0: the native PSGL bodies are off (the lifted ones run), for A/B
 * runs on one executable. */
extern int kz_native_psgl;

/* Calls a lifted guest function from a native body with the guest ABI: the
 * arguments in r3.., the result in r3. The callee may change the volatile
 * registers, as it may for any caller. */
static inline uint64_t kz_guest_call(ppu_context* ctx, void (*fn)(ppu_context*), uint64_t r3)
{
    ctx->gpr[3] = r3;
    fn(ctx);
    return ctx->gpr[3];
}
#endif
