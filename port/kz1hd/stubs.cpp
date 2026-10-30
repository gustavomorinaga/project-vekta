/*
 * Killzone 1 HD -- game-specific hooks and overrides (main.cpp is the generic runner).
 *
 * libsre (cellSpurs/cellSync) stays HLE, except _cellSpursLFQueueInitialize and
 * _cellSpursLFQueuePushBody: the PPU side of a lock-free queue whose consumer is a
 * lifted SPU task. Its protocol lives in guest memory and in the SPU code, so the real
 * libsre functions run instead of a reimplementation. That needs the user's own libsre.prx, relocated to 0x02000000
 * and lifted into LIBSRE_DIR (tools/lift.sh; docs/dev/port-journal.md); the image is read at
 * run time from $KZ_LIBSRE_IMAGE. Without it the push stays unimplemented.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

#include "ppu_recomp.h"
#include "spu_context.h"

#ifdef KZ_LIBSRE
extern "C" {
extern uint8_t* vm_base;
extern const func_entry libsre_function_table[];
extern const uint64_t   libsre_function_table_count;
extern PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
void     ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
static void level_unlocked(ppu_context* ctx) { ctx->gpr[3] = 1; }
static void not_submission(ppu_context* ctx) { ctx->gpr[3] = 0; }
void     ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
uint64_t ppu_guest_call(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
int32_t  _cellSpursSendSignal(void* taskset, uint32_t taskId);
uint32_t cellSpursHleTasksetByWid(uint32_t spurs_ea, uint32_t wid);
}

/* Firmware 4.x libsre at base 0x02000000: OPDs from the export table, code from the OPD. */
static const uint32_t LIBSRE_BASE          = 0x02000000;
static const uint32_t LIBSRE_PUSHBODY_OPD  = 0x0203184C;   /* _cellSpursLFQueuePushBody, NID 0x8A85674D */
static const uint32_t LIBSRE_LFQINIT_OPD   = 0x02031844;   /* _cellSpursLFQueueInitialize, NID 0x011EE38B */
static const uint32_t LIBSRE_SENDSIGNAL    = 0x020127CC;   /* the fpSendSignal PushBody passes (TOC-0x7BC8 -> OPD 0x0203163C) */

/* The queue's memory layout is whatever the real initializer writes, so it runs too. */
static void libsre_call(ppu_context* ctx, uint32_t opd, const char* name)
{
    const uint32_t a0 = (uint32_t)ctx->gpr[3];
    /* On the calling thread's own stack, 4 KB below its frame -- not through
     * ppu_guest_call: that runs on one fixed scratch stack per host thread, so
     * a push made from inside another guest call (the MultiStream loader does)
     * overwrote that call's frames and the queue got a garbage task id. */
    const uint32_t code = vm_read32(opd), toc = vm_read32(opd + 4);
    void (*fn)(ppu_context*) = nullptr;
    for (uint64_t i = 0; i < libsre_function_table_count; i++)
        if (libsre_function_table[i].addr == code) { fn = libsre_function_table[i].func; break; }
    if (!fn) { fprintf(stderr, "[kz] libsre %s: no code at 0x%08X\n", name, code); ctx->gpr[3] = (uint64_t)-1; return; }
    ppu_context c = *ctx;
    c.gpr[1] = (ctx->gpr[1] - 0x1000) & ~0xFull;
    c.gpr[2] = toc;
    c.lr = 0;
    ppu_context* const active = g_active_ctx;   /* store-conditionals check the active context */
    g_active_ctx = &c;
    fn(&c);
    while (g_trampoline_fn) { void (*t)(void*) = g_trampoline_fn; g_trampoline_fn = 0; t(&c); }
    g_active_ctx = active;
    ctx->gpr[3] = c.gpr[3];
    if ((uint32_t)ctx->gpr[3]) fprintf(stderr, "[kz] libsre %s(0x%08X) failed\n", name, a0);
    static int n = 0;
    if (n++ < 16 || (uint32_t)ctx->gpr[3]) fprintf(stderr, "[kz] libsre %s -> 0x%08X\n", name, (uint32_t)ctx->gpr[3]);
}
extern "C" void (*g_spu_dma_hook)(spu_context*, uint32_t, uint32_t, uint32_t, int);
/* KZ_LFQ_WATCH=1 (diagnosis): name every SPU DMA PUT and PPU-visible change
 * that lands in a lock-free queue's first 128 bytes (its header). */
static uint32_t s_lfq[8]; static unsigned s_nlfq;
static void lfq_put_hook(spu_context* spu, uint32_t ea, uint32_t lsa, uint32_t size, int put)
{
    if (!put) return;
    for (unsigned i = 0; i < s_nlfq; i++)
        if (ea < s_lfq[i] + 128 && ea + size > s_lfq[i]) {
            static int n; if (n++ < 64)
                fprintf(stderr, "[lfq-watch] SPU img=%d pc=0x%05X PUT ea=0x%08X size=%u lsa=0x%05X onto queue 0x%08X\n",
                        spu->image_id, (uint32_t)spu->pc & 0x3FFFF, ea, size, lsa, s_lfq[i]);
        }
}
static void lfqueue_init(ppu_context* ctx)
{
    const uint32_t q = (uint32_t)ctx->gpr[4];
    libsre_call(ctx, LIBSRE_LFQINIT_OPD, "_cellSpursLFQueueInitialize");
    fprintf(stderr, "[kz] LFQueue at 0x%08X\n", q);
    if (s_nlfq < 8) s_lfq[s_nlfq++] = q;
    if (getenv("KZ_LFQ_WATCH")) { g_spu_dma_hook = lfq_put_hook; if (s_nlfq < 8) s_lfq[s_nlfq++] = 0x401F2080u; }
}
static void lfqueue_push_body(ppu_context* ctx) { libsre_call(ctx, LIBSRE_PUSHBODY_OPD, "_cellSpursLFQueuePushBody"); }

/* The push wakes the consumer through the fpSendSignal pointer it is handed
 * (cellSpursSendSignal): route that to the HLE, whose WAIT_SIGNAL is what the lifted
 * SPU task is parked in. A queue owned by a SPURS instance rather than a taskset
 * signals (spurs | 1, wid << 8 | task); the real libsre resolves the wid through
 * workload data the HLE does not keep, so the HLE's own taskset table answers. */
static void send_signal(ppu_context* ctx)
{
    uint32_t ea = (uint32_t)ctx->gpr[3], id = (uint32_t)ctx->gpr[4];
    if ((ea & 0xF) == 1) {
        const uint32_t spurs = ea & ~0xFu, wid = id >> 8;
        ea = cellSpursHleTasksetByWid(spurs, wid);
        if (!ea) fprintf(stderr, "[kz] send_signal: no taskset for spurs 0x%08X wid %u (task %u)\n", spurs, wid, id & 0xFF);
        id &= 0xFF;
    }
    ctx->gpr[3] = ea ? (uint64_t)(int64_t)_cellSpursSendSignal((void*)(uintptr_t)ea, id)
                     : (uint64_t)(int64_t)(int32_t)0x80410905;   /* CELL_SPURS_TASK_ERROR_SRCH */
}
#endif

extern "C" {
extern volatile int g_spu_float_mode;   /* spu_channels.c */
void     kz_sail_register(void);   /* sail.cpp: movies through FFmpeg */
void     cellGcmHleSetStrictFlip(int on);
void     cellGcmHleSetIsrThread(int on);
}

void kz_pc_options_register(void);   /* pcoptions.cpp: mouse look, PC options */
extern "C" void spu_begin_image(int image_id);
extern "C" void kzmp3_spu_recomp_register(void) __attribute__((weak));   /* <SPU_DIR>/flashmp3, lift.sh */

extern "C" void ps3_load_prx_modules(void)
{
    kz_sail_register();
    /* KZ1 HD paces on the RSX holding each flip to vblank (KZ_STRICT_FLIP=0 to compare) */
    cellGcmHleSetStrictFlip(!getenv("KZ_STRICT_FLIP") || atoi(getenv("KZ_STRICT_FLIP")));
    /* Its vblank handler waits in the same call its main thread may be waiting in,
     * so it has to run on a thread of its own, as on the PS3. */
    cellGcmHleSetIsrThread(1);
#ifndef KZ_SPU_EXACT_FLOAT
    g_spu_float_mode = 1;   /* round toward zero + FTZ around SPU runs: see CMakeLists, SPU_FAST_FLOAT */
#endif
    /* Unless KZ_JM_SKIP=0, jm_post's jobs never run on the SPUs at all
     * (native/jm.c takes and finishes them): in this port they only ever read
     * empty buffers, and an SPU job reading the depth target stored it to
     * guest memory every frame. ps3recomp v0.18.0's scheduler (#291) runs the
     * other workloads on the instances' own SPUs; its poll period is left at
     * its default (the 50 us of the old kernel thread halved the frame rate). */
    if (!getenv("KZ_JM_SKIP") || strcmp(getenv("KZ_JM_SKIP"), "0"))
        setenv("PS3_SPURS_WID_LANES", "1:0", 0);
    /* The job managers' selectWorkload poll answered with the current workload,
     * so each runs its jobs back to back. Answered 0, jm_gfx (wid 2) went back
     * to the kernel after every job and in busy 144 Hz scenes built the
     * first-person weapon from last frame's bones (docs/dev/ps3recomp-v018-migration.md). */
    setenv("PS3_SPURS_PM_SELECT", "1", 0);
    /* KZ_UNLOCK_LEVELS=1 (testing): every level selectable. The chapter
     * select asks IsLevelUnlocked (func_00298578) through its script binding,
     * an indirect call, which this replaces. */
    if (getenv("KZ_UNLOCK_LEVELS")) ppu_register_function(0x00298578, level_unlocked);
    /* KZ_DEBUG_MENU=1 (testing): the pause menu's own Debug Options (God
     * Mode, Victory Status, Hit Info, the lock and spread circles), which it
     * offers when the build is not a submission one. IsSubmission's script
     * binding is the member function 0x002AE1C4 (return 1; IsCDBuild beside it
     * returns 0), reached through its pointer at 0x00549BC0. */
    if (getenv("KZ_DEBUG_MENU")) ppu_register_function(0x002AE1C4, not_submission);
    kz_pc_options_register();
    /* MultiStream (SPU image 3) decodes the music with the firmware's MP3
     * decoder, flashMP3.pic, which the game reads from dev_flash and loads at
     * LS 0x1A880 (its entry, 0x74A0 in the file, is called at LS 0x21D20).
     * lift.sh lifts it at that base; its functions join image 3's. Without it
     * the audio task ends at the first MP3 and every sound stops. */
    if (kzmp3_spu_recomp_register && !getenv("KZ_NO_MP3")) {
        spu_begin_image(3);
        kzmp3_spu_recomp_register();
        spu_begin_image(0);
    } else fprintf(stderr, "[kz] flashMP3.pic not lifted (lift.sh): no music or sound after the first MP3\n");
#ifdef KZ_LIBSRE
    const char* path = getenv("KZ_LIBSRE_IMAGE");
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f) {
        fprintf(stderr, "[kz] libsre image not loaded (set KZ_LIBSRE_IMAGE): LFQueue push stays unimplemented\n");
        return;
    }
#ifdef _WIN32
    /* the guest space is only reserved on Windows: commit the image's pages
     * (a read into reserved memory fails instead of faulting them in) */
    VirtualAlloc(vm_base + LIBSRE_BASE, 0x100000, MEM_COMMIT, PAGE_READWRITE);
#endif
    size_t n = fread(vm_base + LIBSRE_BASE, 1, 0x100000, f);
    fclose(f);
    for (uint64_t i = 0; i < libsre_function_table_count; i++)
        ppu_register_function(libsre_function_table[i].addr, libsre_function_table[i].func);
    ppu_register_function(LIBSRE_SENDSIGNAL, send_signal);
    ps3_hle_register_ctx(0x011EE38Bu, "_cellSpursLFQueueInitialize (libsre)", lfqueue_init);
    ps3_hle_register_ctx(0x8A85674Du, "_cellSpursLFQueuePushBody (libsre)", lfqueue_push_body);
    fprintf(stderr, "[kz] libsre: %zu bytes at 0x%08X, %llu functions\n", n, LIBSRE_BASE,
            (unsigned long long)libsre_function_table_count);
#endif
}
