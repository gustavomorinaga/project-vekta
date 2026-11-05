/*
 * Offline harness for jm_post's motion blur (docs/dev/wws-jm-post.md, "The motion blur
 * outside the game"), after tools/mlaa_harness: runs the lifted SPU code
 * of any captured jm_post job (Gfx.Post.Downsample de0b, MotionBlur 14e0,
 * Upsample f601, Transpose 85fc, MLAA af58) outside the game.
 *
 * Input: a capture made with KZ_JM_CAPTURE (port/kz1hd/native/jm.c): the
 * registers and local store of each MLAA/Transpose job at its entry (LS
 * 0x4030), and every transfer of the chain's SPU with its bytes. The jobs do
 * their own DMA, so a job is rerun from its entry state against a fake main
 * memory, with the transfers done at once; the policy module's functions the
 * job calls are the lifted ones too.
 *
 *   harness <cap> replay <job>             reruns one job on the bytes its GETs
 *                                          read in the game, compares its PUTs
 *   harness <cap> run <j0> <j1> [ea=file]... [@ea+len=file]...
 *                                          jobs j0..j1 (in capture order) on
 *                                          main memory as their GETs found it
 *                                          (earliest job wins), with each file
 *                                          loaded at its EA first; afterwards
 *                                          each @ range is written to its file
 *                                          (! ranges: before the jobs run)
 *   harness <cap> info                     the jobs: module, GETs, PUTs
 *   harness <cap> recs <job>               one job's transfers
 */
#define SPU_CHANNELS_IMPL   /* the out-of-line spu_wrch is defined here */
#include "spu_context.h"
#include "spu_helpers.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <xmmintrin.h>

/* ---- the runtime surface the lifted code links against ---- */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
volatile int g_spu_direct = 0;          /* every transfer through the drain loop */
uint8_t g_spu_stack_reset_hint[1024];
int g_spu_ls_watch_n = 0;
void spu_ls_watch_slow(uint32_t lsa, int w, const uint8_t* p, uint32_t pc, uint32_t lr) { (void)lsa; (void)w; (void)p; (void)pc; (void)lr; }

typedef void (*spu_fn)(spu_context*);

/* f601's hot loops are native in the game (port/kz1hd/native/f601.c, the
 * lifter leaves <name> to it): here, the lifted bodies */
#define KZ_LIFTED(n) void n##_lifted(spu_context*); void n(spu_context* c) { n##_lifted(c); }
KZ_LIFTED(jobmod_f60183d3d087_e0030_spu_func_00004E60)
KZ_LIFTED(jobmod_f60183d3d087_e0030_spu_func_00004FD0)
KZ_LIFTED(jobmod_f60183d3d087_e0030_spu_func_00005368)
KZ_LIFTED(jobmod_6b716cb6ea57_e0030_spu_func_00005330)
KZ_LIFTED(jobmod_6b716cb6ea57_e0030_spu_func_00005880)
KZ_LIFTED(jobmod_ab05f04899ea_e0030_spu_func_000052B0)
KZ_LIFTED(jobmod_ab05f04899ea_e0030_spu_func_00005AD8)
static spu_fn* s_reg;                    /* table the register calls fill */
enum { NMOD = 8 };
static spu_fn s_pm[0x10000], s_mod[NMOD][0x10000];   /* by LS address / 4 */
/* overlay id, code extent (the module images registered by FUN_000F3298) */
static const int s_mod_id[NMOD] = { 203, 206, 200, 207, 209, 201, 202, 204 };
static uint32_t s_mod_end[NMOD] = { 0x4000 + 0x3110, 0xB090, 0x4000 + 0x2110, 0x4000 + 0x1910, 0x4000 + 0x1C90,
                                    0x4000 + 0x2010, 0x4000 + 0x1890, 0x4000 + 0x2590 };
static int s_cur;
void spu_register_function(uint32_t addr, spu_fn fn) { if (addr < SPU_LS_SIZE) s_reg[addr >> 2] = fn; }

static jmp_buf s_halt;
static uint8_t* s_mem;                   /* 4 GB fake main memory */
static FILE* s_put_log;                  /* replay: the job's PUTs */
static unsigned long s_unknown;

static FILE* s_trace;                   /* HARNESS_TRACE: every transfer */
static unsigned long* s_prof;           /* HARNESS_PROF: transfers by target */
static spu_fn lookup(uint32_t pc)
{
    pc &= SPU_LS_MASK;
    if (pc >= 0x4000 && pc < s_mod_end[s_cur]) return s_mod[s_cur][pc >> 2];
    if (pc < 0x4000) return s_pm[pc >> 2];
    return 0;
}
void spu_indirect_branch(spu_context* ctx)
{
    ctx->pc &= SPU_LS_MASK;
    if (ctx->pc == SPURS_PM_SELECT_WORKLOAD_LS) {   /* cellSpursModulePoll, as the runtime answers it: keep running */
        const uint32_t cur = (uint32_t)ctx->ls[0x1DC] << 24 | (uint32_t)ctx->ls[0x1DD] << 16 | (uint32_t)ctx->ls[0x1DE] << 8 | ctx->ls[0x1DF];
        ctx->gpr[3] = spu_make_preferred_u32(cur < 16 ? cur : 0);
        ctx->pc = ctx->gpr[0]._u32[0] & SPU_LS_MASK;
        return;
    }
    spu_fn f = lookup(ctx->pc);
    if (s_prof) s_prof[ctx->pc]++;
    if (s_trace) fprintf(s_trace, "C %05X d%u\n", ctx->pc, ctx->host_depth);
    if (!f) { fprintf(stderr, "harness: no lifted function at LS 0x%05X\n", ctx->pc); longjmp(s_halt, 2); }
    f(ctx);
}
void spu_drain_call(spu_context* ctx, uint32_t ret)
{
    for (;;) {
        while (g_spu_trampoline_fn) {
            if (g_spu_trampoline_fn == spu_indirect_branch && (ctx->pc & SPU_LS_MASK) == (ret & SPU_LS_MASK)) { g_spu_trampoline_fn = 0; return; }
            spu_fn f = g_spu_trampoline_fn; g_spu_trampoline_fn = 0;
            if (s_prof) s_prof[ctx->pc & SPU_LS_MASK]++;
            if (s_trace) fprintf(s_trace, "T %05X d%u\n", ctx->pc & SPU_LS_MASK, ctx->host_depth);
            f(ctx);
        }
        if ((ctx->pc & SPU_LS_MASK) == (ret & SPU_LS_MASK)) return;
        g_spu_trampoline_fn = spu_indirect_branch;   /* a host return elsewhere: go on from pc */
    }
}
void spu_img_restore(spu_context* ctx, int32_t s) { (void)ctx; (void)s; }
void spu_stop(spu_context* ctx) { fprintf(stderr, "harness: stop 0x%X at 0x%05X\n", ctx->stop_code, ctx->pc); longjmp(s_halt, 3); }
void spu_halt(spu_context* ctx) { (void)ctx; longjmp(s_halt, 4); }

static uint32_t s_dec = 0xFFFFFFFFu;
u128 spu_rdch(spu_context* ctx, uint32_t ch)
{
    u128 r; memset(&r, 0, sizeof r);
    switch (ch) {
    case SPU_RdDec: s_dec -= 100; r._u32[0] = s_dec; break;
    case MFC_RdTagStat: r._u32[0] = ctx->mfc_tag_mask; break;   /* every transfer is done */
    case MFC_RdAtomicStat: r._u32[0] = ctx->atomic_stat; break;
    case SPU_RdEventStat: r._u32[0] = ctx->event_status & ctx->event_mask; break;
    case SPU_RdEventMask: r._u32[0] = ctx->event_mask; break;
    case SPU_RdMachStat: r._u32[0] = 0; break;
    case MFC_RdListStallStat: r._u32[0] = 0; break;
    default: if (s_unknown++ < 8) fprintf(stderr, "harness: rdch %u at 0x%05X\n", ch, ctx->pc);
    }
    return r;
}
uint32_t spu_rchcnt(spu_context* ctx, uint32_t ch)
{
    (void)ctx;
    switch (ch) {
    case SPU_RdEventStat: return (ctx->event_status & ctx->event_mask) != 0;
    default: return 1;
    }
}

static void xfer(spu_context* ctx, uint32_t lsa, uint32_t ea, uint32_t size, int put)
{
    lsa &= SPU_LS_MASK;
    if (lsa + size > SPU_LS_SIZE) { fprintf(stderr, "harness: DMA past LS end\n"); longjmp(s_halt, 5); }
    { static int x = -1; if (x < 0) x = getenv("HARNESS_XFER") != NULL;
      if (x) fprintf(stderr, "%c %08X %05X %X\n", put ? 'P' : 'G', ea, lsa, size); }
    if (put) {
        memcpy(s_mem + ea, ctx->ls + lsa, size);
        if (s_put_log) { fwrite(&ea, 4, 1, s_put_log); fwrite(&size, 4, 1, s_put_log); fwrite(ctx->ls + lsa, 1, size, s_put_log); }
    } else memcpy(ctx->ls + lsa, s_mem + ea, size);
}
void spu_wrch(spu_context* ctx, uint32_t ch, u128 v)
{
    const uint32_t x = v._u32[0];
    switch (ch) {
    case MFC_LSA: ctx->mfc_lsa = x; return;
    case MFC_EAH: ctx->mfc_eah = x; return;
    case MFC_EAL: ctx->mfc_eal = x; return;
    case MFC_Size: ctx->mfc_size = x; return;
    case MFC_TagID: ctx->mfc_tag = x & 31; return;
    case MFC_WrTagMask: ctx->mfc_tag_mask = x; return;
    case MFC_WrTagUpdate: case SPU_WrDec: case SPU_WrEventAck: case MFC_WrListStallAck: case MFC_WrMSSyncReq: return;
    case SPU_WrEventMask: ctx->event_mask = x; return;
    case MFC_Cmd: break;
    default: if (s_unknown++ < 8) fprintf(stderr, "harness: wrch %u = 0x%X at 0x%05X\n", ch, x, ctx->pc); return;
    }
    const uint32_t cmd = x & 0xFF, lsa = ctx->mfc_lsa, ea = ctx->mfc_eal, size = ctx->mfc_size;
    switch (cmd) {
    case MFC_GET_CMD: case MFC_GETB_CMD: case MFC_GETF_CMD: xfer(ctx, lsa, ea, size, 0); return;
    case MFC_PUT_CMD: case MFC_PUTB_CMD: case MFC_PUTF_CMD: xfer(ctx, lsa, ea, size, 1); return;
    case MFC_GETL_CMD: case MFC_GETLB_CMD: case MFC_GETLF_CMD:
    case MFC_PUTL_CMD: case MFC_PUTLB_CMD: case MFC_PUTLF_CMD: {
        /* the list (in LS at the EA register) as mfc_run_list runs it */
        const int put = (cmd & 0xF0) == 0x20;
        uint32_t dest = lsa;
        for (uint32_t i = 0; i < size / 8; i++) {
            const uint32_t el = (ea + 8 * i) & SPU_LS_MASK;
            const uint32_t sf = spu_ls_read32(ctx, el), eal = spu_ls_read32(ctx, el + 4), n = sf & 0x7FFF;
            /* Stall-and-notify: only the policy module's prefetch of the next
             * job uses it (a size-0 sentinel ahead of the descriptor, pm
             * 0x2874); its interrupt only moves the job manager's own load
             * state, so the list just runs on. */
            if ((sf >> 31) && n) { fprintf(stderr, "harness: stall-and-notify element with data\n"); longjmp(s_halt, 6); }
            if (n) { xfer(ctx, (dest & ~15u) | (eal & 15u), eal, n, put); dest = (dest & ~15u) + ((n + 15u) & ~15u); }
        }
        return; }
    case MFC_GETLLAR_CMD:
        memcpy(ctx->ls + (lsa & SPU_LS_MASK & ~127u), s_mem + (ea & ~127u), 128); ctx->atomic_stat = 4; return;
    case MFC_PUTLLC_CMD: case MFC_PUTLLUC_CMD: case MFC_PUTQLLUC_CMD:
        memcpy(s_mem + (ea & ~127u), ctx->ls + (lsa & SPU_LS_MASK & ~127u), 128); ctx->atomic_stat = cmd == MFC_PUTLLC_CMD ? 0 : 2; return;
    case MFC_BARRIER_CMD: case MFC_EIEIO_CMD: case MFC_SYNC_CMD: return;
    }
    fprintf(stderr, "harness: MFC command 0x%X at 0x%05X\n", cmd, ctx->pc);
    longjmp(s_halt, 7);
}

/* ---- the capture ---- */
typedef struct { char put; uint32_t ea, lsa, size; long off; int job; long seq; } rec;
typedef struct { int module; long first, last; } job;   /* its records: [first, last) */
static rec* s_rec; static long s_nrec;
static job s_job[4096]; static int s_njob;
static FILE* s_bin;
static const char* s_cap;

/* Records go to the job their SPU (the ctx column) is running: with several
 * lanes the log interleaves jobs. The policy module prefetches the next job
 * (its descriptor, a GET into LS 0xC00, then the descriptor's input list)
 * while the current one runs: records on a ctx from that descriptor GET to the
 * next job entry belong to the next job. Records are then grouped by job. */
static int rec_cmp(const void* a, const void* b)
{
    const rec *x = a, *y = b;
    return x->job != y->job ? (x->job < y->job ? -1 : 1) : (x->seq < y->seq ? -1 : x->seq > y->seq);
}
static void load_capture(const char* dir)
{
    char p[1024]; snprintf(p, sizeof p, "%s/dma.log", dir);
    FILE* f = fopen(p, "r");
    if (!f) { perror(p); exit(1); }
    snprintf(p, sizeof p, "%s/dma.bin", dir); s_bin = fopen(p, "rb");
    long cap = 1 << 20; s_rec = malloc(cap * sizeof *s_rec);
    enum { NCTX = 8 };
    char line[256], ctxn[NCTX][32]; int ctxj[NCTX], nctx = 0;
    long* pend[NCTX]; long npend[NCTX]; int pending[NCTX];
    for (int i = 0; i < NCTX; i++) { pend[i] = malloc(sizeof(long) << 20); npend[i] = 0; pending[i] = 0; }
    while (fgets(line, sizeof line, f)) {
        char ctxs[32], k; unsigned a, b, c; long o; int m, n;
        if (sscanf(line, "%31s %c", ctxs, &k) != 2) continue;
        int ci = 0;
        while (ci < nctx && strcmp(ctxn[ci], ctxs)) ci++;
        if (ci == nctx) { if (nctx == NCTX) continue; strcpy(ctxn[nctx], ctxs); ctxj[nctx++] = -1; }
        if (k == 'J' && sscanf(line, "%31s J %d %d", ctxs, &m, &n) == 3) {
            /* the Transpose and MLAA jobs do their own DMA; the others take
             * their input from the list */
            const int pre = m != 203 && m != 206;
            for (long i = 0; i < npend[ci]; i++) s_rec[pend[ci][i]].job = pre ? s_njob : ctxj[ci];
            ctxj[ci] = s_njob; s_job[s_njob].module = m;
            npend[ci] = 0; pending[ci] = 0; s_njob++;
            continue;
        }
        if (sscanf(line, "%31s %c %x %x %x %lx", ctxs, &k, &a, &b, &c, &o) != 6) continue;
        if (k == 'G' && b == 0xC00 && c >= 0x70 && c <= 0xA0) pending[ci] = 1;
        const int pg = pending[ci] && k == 'G';   /* PUTs are the running job's */
        if (!pg && ctxj[ci] < 0) continue;
        if (s_nrec == cap) { cap *= 2; s_rec = realloc(s_rec, cap * sizeof *s_rec); }
        s_rec[s_nrec] = (rec){ k, a, b, c, o, pg ? -1 : ctxj[ci], s_nrec };
        if (pg) pend[ci][npend[ci]++] = s_nrec;
        s_nrec++;
    }
    long keep = 0;   /* drop the prefetches of jobs the capture did not reach */
    for (long i = 0; i < s_nrec; i++) if (s_rec[i].job >= 0) s_rec[keep++] = s_rec[i];
    s_nrec = keep;
    qsort(s_rec, s_nrec, sizeof *s_rec, rec_cmp);
    for (int j = 0; j < s_njob; j++) s_job[j].first = s_job[j].last = 0;
    for (long i = s_nrec - 1; i >= 0; i--) s_job[s_rec[i].job].first = i;
    for (long i = 0; i < s_nrec; i++) s_job[s_rec[i].job].last = i + 1;
    for (int i = 0; i < NCTX; i++) free(pend[i]);
    fclose(f);
    s_cap = dir;
}
static void rec_bytes(const rec* r, uint8_t* d) { fseek(s_bin, r->off, SEEK_SET); if (fread(d, 1, r->size, s_bin) != r->size) exit(2); }

static spu_context* s_ctx;
static int run_job(int j)
{
    char p[1024]; snprintf(p, sizeof p, "%s/job%03d.bin", s_cap, j);
    FILE* f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    spu_context* ctx = s_ctx;
    memset(ctx, 0, sizeof *ctx);
    ctx->ls = ctx->ls_store;
    if (fread(ctx->gpr, 16, 128, f) != 128 || fread(ctx->ls, 1, SPU_LS_SIZE, f) != SPU_LS_SIZE) { fprintf(stderr, "%s: short\n", p); exit(1); }
    fclose(f);
    s_cur = -1;
    for (int m = 0; m < NMOD; m++) if (s_mod_id[m] == s_job[j].module) s_cur = m;
    if (s_cur < 0) { fprintf(stderr, "job %d: module %d not linked\n", j, s_job[j].module); exit(1); }
    /* all but 85fc/af58 get their input from the descriptor's DMA list,
     * which the policy module runs before the job starts (the capture's LS
     * already holds it): redone here from main memory, so inputs loaded with
     * `run` reach them. The jobs' own transfers come after and redo theirs. */
    if (s_job[j].module != 203 && s_job[j].module != 206)
        for (long i = s_job[j].first; i < s_job[j].last; i++)
            if (s_rec[i].put == 'G' && s_rec[i].lsa != 0xC00 && s_rec[i].lsa + s_rec[i].size <= SPU_LS_SIZE)
                memcpy(ctx->ls + s_rec[i].lsa, s_mem + s_rec[i].ea, s_rec[i].size);
    ctx->policy_mode = 1; ctx->resident_ovl = s_job[j].module;
    ctx->pc = 0x4030; ctx->host_depth = 1;
    const uint32_t ret = ctx->gpr[0]._u32[0] & SPU_LS_MASK;
    const int r = setjmp(s_halt);
    if (r) return r;
    g_spu_trampoline_fn = 0;
    /* the float mode the runtime sets around an SPU run (spu_float_mode_enter):
     * round toward zero, flush to zero, denormals are zero, as the SPU's
     * single precision; it matters with SPU_FAST_FLOAT (host IEEE ops) */
    _mm_setcsr((_mm_getcsr() & ~0x6000u) | 0x6000u | 0x8040u);
    spu_indirect_branch(ctx);
    spu_drain_call(ctx, ret);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: harness <cap> replay <job> | run <j0> <j1> [ea=file]... [@ea+len=file]... | info\n"); return 1; }
    extern void jobmod_85fc8580d65e_e0030_spu_recomp_register(void);
    extern void jobmod_af58da8c8c70_e0030_spu_recomp_register(void);
    extern void jobmod_14e0d6b1737e_e0030_spu_recomp_register(void);
    extern void jobmod_de0b974339a8_e0030_spu_recomp_register(void);
    extern void jobmod_f60183d3d087_e0030_spu_recomp_register(void);
    extern void jobmod_6b716cb6ea57_e0030_spu_recomp_register(void);
    extern void jobmod_8197c3b77d98_e0030_spu_recomp_register(void);
    extern void jobmod_ab05f04899ea_e0030_spu_recomp_register(void);
    extern void pm_005B9180_spu_recomp_register(void);
    s_reg = s_pm; pm_005B9180_spu_recomp_register();
    s_reg = s_mod[0]; jobmod_85fc8580d65e_e0030_spu_recomp_register();
    s_reg = s_mod[1]; jobmod_af58da8c8c70_e0030_spu_recomp_register();
    s_reg = s_mod[2]; jobmod_14e0d6b1737e_e0030_spu_recomp_register();
    s_reg = s_mod[3]; jobmod_de0b974339a8_e0030_spu_recomp_register();
    s_reg = s_mod[4]; jobmod_f60183d3d087_e0030_spu_recomp_register();
    s_reg = s_mod[5]; jobmod_6b716cb6ea57_e0030_spu_recomp_register();
    s_reg = s_mod[6]; jobmod_8197c3b77d98_e0030_spu_recomp_register();
    s_reg = s_mod[7]; jobmod_ab05f04899ea_e0030_spu_recomp_register();
    s_mem = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    s_ctx = aligned_alloc(64, (sizeof(spu_context) + 63) & ~63ul);
    load_capture(argv[1]);
    if (getenv("HARNESS_TRACE")) s_trace = fopen(getenv("HARNESS_TRACE"), "w");
    if (getenv("HARNESS_PROF")) s_prof = calloc(SPU_LS_SIZE, sizeof *s_prof);
    uint8_t* tmp = malloc(1 << 20);

    if (!strcmp(argv[2], "info")) {
        for (int j = 0; j < s_njob; j++) {
            long g = 0, p = 0; uint32_t g0 = 0, p0 = 0;
            for (long i = s_job[j].first; i < s_job[j].last; i++) {
                if (s_rec[i].put == 'G') { if (!g++) g0 = s_rec[i].ea; } else { if (!p++) p0 = s_rec[i].ea; }
            }
            printf("job %3d module %d: %ld GETs (first %08X), %ld PUTs (first %08X)\n", j, s_job[j].module, g, g0, p, p0);
        }
        return 0;
    }
    if (!strcmp(argv[2], "recs") && argc > 3) {   /* one job's records */
        const int j = atoi(argv[3]);
        for (long i = s_job[j].first; i < s_job[j].last; i++)
            printf("%c %08X %05X %X\n", s_rec[i].put, s_rec[i].ea, s_rec[i].lsa, s_rec[i].size);
        return 0;
    }
    if (!strcmp(argv[2], "replay") && argc > 3) {
        const int j = atoi(argv[3]);
        for (long i = s_job[j].first; i < s_job[j].last; i++)   /* what the job's GETs read */
            if (s_rec[i].put == 'G') { rec_bytes(&s_rec[i], tmp); memcpy(s_mem + s_rec[i].ea, tmp, s_rec[i].size); }
        s_put_log = tmpfile();
        const int r = run_job(j);
        if (r) { fprintf(stderr, "job %d halted (%d)\n", j, r); return 1; }
        /* the PUTs, in order, against the game's */
        rewind(s_put_log);
        long k = s_job[j].first, n = 0, bad = 0, extra = 0;
        uint32_t hd[2];
        while (fread(hd, 4, 2, s_put_log) == 2) {
            if (fread(tmp + (1 << 19), 1, hd[1], s_put_log) != hd[1]) break;
            while (k < s_job[j].last && s_rec[k].put != 'P') k++;
            if (k >= s_job[j].last) { extra++; continue; }
            rec_bytes(&s_rec[k], tmp);
            if (s_rec[k].ea != hd[0] || s_rec[k].size != hd[1] || memcmp(tmp, tmp + (1 << 19), hd[1])) {
                if (bad++ < 5) fprintf(stderr, "PUT %ld: harness %08X/%X, game %08X/%X\n", n, hd[0], hd[1], s_rec[k].ea, s_rec[k].size);
            }
            k++; n++;
        }
        long missing = 0;
        for (; k < s_job[j].last; k++) if (s_rec[k].put == 'P') missing++;
        printf("job %d (module %d): %ld PUTs, %ld differ, %ld extra, %ld missing\n", j, s_job[j].module, n, bad, extra, missing);
        return bad || extra || missing;
    }
    if (!strcmp(argv[2], "run") && argc > 4) {
        const int j0 = atoi(argv[3]), j1 = atoi(argv[4]);
        for (int j = j1; j >= j0; j--)   /* the earliest GET of a byte wins */
            for (long i = s_job[j].last - 1; i >= s_job[j].first; i--)
                if (s_rec[i].put == 'G') { rec_bytes(&s_rec[i], tmp); memcpy(s_mem + s_rec[i].ea, tmp, s_rec[i].size); }
        for (int a = 5; a < argc; a++) {
            unsigned ea; char path[1024];
            if (argv[a][0] != '@' && sscanf(argv[a], "%x=%1023s", &ea, path) == 2) {
                FILE* f = fopen(path, "rb"); if (!f) { perror(path); return 1; }
                size_t n = fread(s_mem + ea, 1, 64u << 20, f); fclose(f);
                fprintf(stderr, "loaded %zu bytes at %08X\n", n, ea);
            }
        }
        for (int a = 5; a < argc; a++) {   /* !ea+len=file: written before the jobs run */
            unsigned ea, len; char path[1024];
            if (sscanf(argv[a], "!%x+%x=%1023s", &ea, &len, path) == 3) {
                FILE* f = fopen(path, "wb"); fwrite(s_mem + ea, 1, len, f); fclose(f);
            }
        }
        for (int j = j0; j <= j1; j++) {
            const int r = run_job(j);
            if (r) { fprintf(stderr, "job %d halted (%d)\n", j, r); return 1; }
        }
        for (int a = 5; a < argc; a++) {
            unsigned ea, len; char path[1024];
            if (sscanf(argv[a], "@%x+%x=%1023s", &ea, &len, path) == 3) {
                FILE* f = fopen(path, "wb"); fwrite(s_mem + ea, 1, len, f); fclose(f);
            }
        }
        if (s_prof) for (int i = 0; i < SPU_LS_SIZE; i++) if (s_prof[i]) fprintf(stderr, "prof %05X %lu\n", i, s_prof[i]);
        return 0;
    }
    fprintf(stderr, "bad arguments\n");
    return 1;
}
