/* kz_native_run: see native.h. */
#include "native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t s_all[4] = { ~0u, ~0u, ~0u, ~0u };

void kz_native_run(spu_context* ctx, kz_spu_fn native, kz_spu_fn lifted, const char* name)
{
    kz_native_run_live(ctx, native, lifted, name, s_all);
}

void kz_native_run_live(spu_context* ctx, kz_spu_fn native, kz_spu_fn lifted, const char* name,
                        const uint32_t live[4])
{
    static int verify = -1;
    if (verify < 0) verify = getenv("KZ_NATIVE_VERIFY") ? 1 : 0;
    if (!verify) { native(ctx); return; }
    /* The reference: the lifted body on a copy of the registers and LS. */
    static _Thread_local spu_context* ref;
    if (!ref) ref = (spu_context*)malloc(sizeof *ref);
    memcpy(ref, ctx, sizeof *ref);
    ref->ls = ref->ls_store;
    memcpy(ref->ls_store, ctx->ls, SPU_LS_SIZE);
    lifted(ref);
    native(ctx);
    int bad_r = -1, bad_ls = -1;
    for (int r = 0; r < 128 && bad_r < 0; r++)
        if (((live[r >> 5] >> (r & 31)) & 1) && memcmp(&ref->gpr[r], &ctx->gpr[r], 16)) bad_r = r;
    for (int i = 0; i < SPU_LS_SIZE && bad_ls < 0; i += 16)
        if (memcmp(ref->ls_store + i, ctx->ls + i, 16)) bad_ls = i;
    static unsigned long calls, bad;
    calls++;
    if (bad_r >= 0 || bad_ls >= 0) {
        if (bad++ < 20)
            fprintf(stderr, "[kz-native] %s MISMATCH (call %lu): first reg r%d, first LS 0x%05X\n",
                    name, calls, bad_r, bad_ls < 0 ? 0 : bad_ls);
        memcpy(ctx->gpr, ref->gpr, sizeof ctx->gpr);
        memcpy(ctx->ls, ref->ls_store, SPU_LS_SIZE);
    }
    if ((calls & 0xFFF) == 0)
        fprintf(stderr, "[kz-native] %s: %lu calls verified, %lu mismatches\n", name, calls, bad);
}
