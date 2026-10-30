/*
 * jm_post (WWS job manager, workload 1, the post-processing chain) on host
 * threads. See docs/dev/wws-jm-post.md.
 *
 * KZ_JM_HLE=<n> (default 3, 0 = off): n host threads take jobs from the chain
 * next to the three SPU lanes; a job with a native version below runs as
 * native code, any other one on a private copy of the job manager
 * (KZ_JM_HLE_PM, default on; 0 = natives only).
 * KZ_JM_LOG=<file>: one line per job (module, parameters, DMA lists,
 * descriptor), to map each module's parameters.
 * KZ_JM_VERIFY=1: for the modules that have a native version, compute the
 * job's output from guest memory as the job starts on an SPU lane and compare
 * it with what the job writes back (its PUTs), byte for byte.
 * KZ_JM_TIME=1: time per job of each module on the SPU lanes.
 * KZ_JM_WATCH=1: the job manager's header every second.
 * KZ_JM_SKIP_US=<us>: under KZ_JM_SKIP, each job is held that long before it
 * is finished. Off by default. In the 144 Hz mode, in busy scenes, jm_post
 * finishing at once lets the skinned models (jm_gfx) be built from last
 * frame's bone matrices, and parts of the first-person weapon jump about;
 * 10 cures it at ~15% of the frame rate (docs/dev/ps3recomp-v018-migration.md).
 * KZ_JM_DUMPIMG=<flip>: (with KZ_JM_WATCH) the chain's main buffers as PPMs in
 * /tmp/claude-1000 once that many flips went by.
 * KZ_JM_CAPTURE=<dir>: once <dir>/go exists, for 3 flips, every transfer of
 * jm_post's SPUs goes to <dir>/dma.log (one line each: ctx, get/put, EA, LSA,
 * size, offset of its bytes in <dir>/dma.bin), and each MLAA/Transpose job
 * entry (modules 203, 206) adds a line and writes the 128 registers and the
 * local store to <dir>/job<n>.bin: what tools/mlaa_harness replays.
 */
#include "native.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

extern uint8_t* vm_base;
extern void (*g_spu_dma_hook)(spu_context*, uint32_t, uint32_t, uint32_t, int);
extern void (*g_spu_job_hook)(spu_context*);

static inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline uint32_t gw32(uint32_t ea) { return be32(vm_base + ea); }

/* A job as its descriptor describes it. */
typedef struct {
    int      module;            /* overlay id, 200.. */
    uint32_t desc_ea, desc_n;
    uint8_t  desc[0xA0];
    uint32_t list_ea, list_n;   /* input DMA list: list_n entries of (size, ea) */
    const uint8_t* param;       /* 48 bytes after the commands */
} kz_job;

/* What a native computed for the job running on a context: rows of output. */
typedef struct {
    spu_context* ctx;
    int      module, active;
    uint32_t out_ea, row_bytes, rows, pitch;
    uint8_t* data;              /* rows * row_bytes */
    uint32_t desc_ea, desc_n;
    uint8_t  desc[0xA0];
    uint64_t t0; int t_module;  /* KZ_JM_TIME: start of the job running here */
} kz_slot;

#define KZ_SLOTS 16
static kz_slot s_slot[KZ_SLOTS];
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE* s_log;
static int s_verify;

static kz_slot* slot_of(spu_context* ctx)
{
    for (int i = 0; i < KZ_SLOTS; i++) if (s_slot[i].ctx == ctx) return &s_slot[i];
    pthread_mutex_lock(&s_mu);
    kz_slot* r = NULL;
    for (int i = 0; i < KZ_SLOTS && !r; i++) if (!s_slot[i].ctx) { s_slot[i].ctx = ctx; r = &s_slot[i]; }
    pthread_mutex_unlock(&s_mu);
    return r;
}

/* Parses the commands: the DMA list and where the parameters start. */
static int parse_desc(const uint8_t* desc, uint32_t desc_ea, uint32_t desc_n, int module, kz_job* j)
{
    memset(j, 0, sizeof *j);
    j->module = module; j->desc_ea = desc_ea; j->desc_n = desc_n;
    memcpy(j->desc, desc, desc_n);
    uint32_t end = 0;
    for (uint32_t o = 0; o + 8 <= desc_n; o += 8) {
        const uint32_t c = be32(j->desc + o), a = be32(j->desc + o + 4);
        if ((c & 0xFFFF) == 0xC440 || (c & 0xFFFF) == 0xC460) { j->list_ea = a; j->list_n = (c >> 16) / 2; }
        if (c == 0x0A00) { end = o + 8; break; }
    }
    if (!end) return 0;
    end = (end + 15) & ~15u;
    if (end + 48 > desc_n) return 0;
    j->param = j->desc + end;
    return 1;
}
static int parse_job(const kz_slot* s, int module, kz_job* j) { return parse_desc(s->desc, s->desc_ea, s->desc_n, module, j); }

/* ---- natives: each returns 1 and fills the slot when it knows the job ---- */
/* A native computes the job's output rows into s->data (the job may write over
 * its own input, as the SPU does only after reading all of it). */
static uint8_t* out_rows(kz_slot* s, uint32_t out_ea, uint32_t row_bytes, uint32_t rows, uint32_t pitch, uint32_t* stride)
{
    s->out_ea = out_ea; s->row_bytes = row_bytes; s->rows = rows; s->pitch = pitch;
    free(s->data); s->data = (uint8_t*)malloc((size_t)row_bytes * rows);
    *stride = row_bytes;
    return s->data;
}
/* The modes the natives were checked in (see native_vblur/native_hblur). */
static int blur_ok(const uint8_t* p)
{
    const uint32_t f = be32(p + 24) & 0xFFFF;
    return be32(p + 32) == 0x04000000 && be32(p + 20) == 0x00A0003C && (f & ~0x200Fu) == 0x30;
}
static inline __m256i ld16(const uint8_t* p) { return _mm256_cvtepu8_epi16(_mm_loadu_si128((const __m128i*)p)); }

/* 7 taps [1 6 15 20 15 6 1] / 64 over a, b, c, d, e, f, g (c..e centre 16 bytes) */
static inline __m128i blur7(__m256i m3, __m256i m2, __m256i m1, __m256i c, __m256i p1, __m256i p2, __m256i p3)
{
    __m256i v = _mm256_mullo_epi16(c, _mm256_set1_epi16(20));
    v = _mm256_add_epi16(v, _mm256_mullo_epi16(_mm256_add_epi16(m1, p1), _mm256_set1_epi16(15)));
    v = _mm256_add_epi16(v, _mm256_mullo_epi16(_mm256_add_epi16(m2, p2), _mm256_set1_epi16(6)));
    v = _mm256_srli_epi16(_mm256_add_epi16(v, _mm256_add_epi16(m3, p3)), 6);
    return _mm_packus_epi16(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
}

/* 6b71: vertical blur. Input: list of h+6 rows of w pixels; output rows at param. */
static int native_vblur(const kz_job* j, kz_slot* s)
{
    const uint8_t* p = j->param + 16;
    const uint32_t out = be32(p), w = be32(p + 4) >> 16, h = be32(p + 4) & 0xFFFF, pitch = be32(p + 8) >> 16;
    if (j->list_n != h + 6 || w * 4 > pitch || be32(j->param) != 0xA04200C4 || !blur_ok(j->param)) return 0;
    uint32_t rows[256];
    for (uint32_t i = 0; i < j->list_n; i++) { rows[i] = gw32(j->list_ea + 8 * i + 4); if (gw32(j->list_ea + 8 * i) != w * 4) return 0; }
    if (be32(p + 8) & 8) for (uint32_t i = h + 3; i < h + 6; i++) rows[i] = rows[h + 2];   /* bottom edge: clamp */
    if (!s) return 1;
    uint32_t st; uint8_t* d = out_rows(s, out, w * 4, h, pitch, &st);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w * 4; x += 16) {
            const uint8_t* r[7];
            for (int t = 0; t < 7; t++) r[t] = vm_base + rows[y + t] + x;
            _mm_storeu_si128((__m128i*)(d + y * st + x),
                             blur7(ld16(r[0]), ld16(r[1]), ld16(r[2]), ld16(r[3]), ld16(r[4]), ld16(r[5]), ld16(r[6])));
        }
    return 1;
}

/* ab05: horizontal blur. Input: h rows of w + 8 pixels (4 each side). */
static int native_hblur(const kz_job* j, kz_slot* s)
{
    const uint8_t* p = j->param + 16;
    const uint32_t out = be32(p), w = be32(p + 4) >> 16, h = be32(p + 4) & 0xFFFF, pitch = be32(p + 8) >> 16;
    if (j->list_n != h || w * 4 > pitch || be32(j->param) != 0xA83C2004 || !blur_ok(j->param)) return 0;
    for (uint32_t y = 0; y < h; y++) if (gw32(j->list_ea + 8 * y) != (w + 8) * 4) return 0;
    if (!s) return 1;
    uint32_t st; uint8_t* d = out_rows(s, out, w * 4, h, pitch, &st);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* r = vm_base + gw32(j->list_ea + 8 * y + 4) + 16;   /* centre of output pixel 0 */
        for (uint32_t x = 0; x < w * 4; x += 16)
            _mm_storeu_si128((__m128i*)(d + y * st + x),
                             blur7(ld16(r + x - 12), ld16(r + x - 8), ld16(r + x - 4), ld16(r + x),
                                   ld16(r + x + 4), ld16(r + x + 8), ld16(r + x + 12)));
    }
    return 1;
}

/* With out NULL: only says whether a native handles the job. */
static int native_job(const kz_job* j, kz_slot* out)
{
    switch (j->module) {
    case 201: return native_vblur(j, out);
    case 204: return native_hblur(j, out);
    }
    return 0;
}

/* ---- KZ_JM_HLE=<n>: n host threads run the chain's jobs that have a native ----
 *
 * They take jobs from the job manager's list as three more virtual SPUs (slots
 * 3..5 of the list's row) by the same rules as the policy module's allocator
 * (pm 0x23C8): under the lock-line lock, the command at the row's ticket is
 * taken (ticket + 1, u16 0 = max(u16 0, ticket + 1)); a job also puts its
 * index in the SPU's slot, and a barrier is passed only once the u16 count of
 * its object is 0. A finished job writes its output, decrements its barrier
 * object (descriptor command 0x0800) and frees its slot (0xFFFF). Commands
 * they cannot run are left to the SPUs. */
/* jm_post's header (its workload argument) and its SPURS instance, "HiPrio":
 * taken when the game adds the workload (g_spurs_workload_added_hook), since
 * the heap layout differs between runtime versions. 0 until then. */
static volatile uint32_t s_jm_hdr, s_jm_spurs, s_gfx_hdr;
static unsigned long s_gfx_waits, s_gfx_timeouts;
#define JM_HDR s_jm_hdr
static const struct { uint32_t code_ea; int module; } s_code[] = {
    { 0x005AB500u, 201 }, { 0x005A8F00u, 204 },
};
extern void spu_lockline_lock(void);
extern void spu_lockline_unlock(void);
extern void spu_coh_notify_write(uint32_t ea);
extern void (*g_spu_line_commit_hook)(uint32_t line);
static inline uint16_t gr16(uint32_t ea) { return (uint16_t)(vm_base[ea] << 8 | vm_base[ea + 1]); }
static inline void gw16(uint32_t ea, uint16_t v) { vm_base[ea] = (uint8_t)(v >> 8); vm_base[ea + 1] = (uint8_t)v; }
static inline int guest_ok(uint32_t ea, uint32_t n) { return ea >= 0x10000u && (uint64_t)ea + n <= 0xE0000000u; }
static inline void gw32w(uint32_t ea, uint32_t v) { vm_base[ea] = (uint8_t)(v >> 24); vm_base[ea + 1] = (uint8_t)(v >> 16); vm_base[ea + 2] = (uint8_t)(v >> 8); vm_base[ea + 3] = (uint8_t)v; }
static unsigned long s_hle_jobs, s_hle_pm_jobs;
static int s_hle_pm;   /* KZ_JM_HLE_PM: jobs without a native run on a private job manager */
/* KZ_JM_SKIP (default on, 0 = off): jobs are taken and finished (tickets, slots,
 * barriers) without running. In this port the chain reads two half-resolution
 * targets the RSX draws into main memory, which the host renderer never stores
 * there, and nothing the RSX draws samples its output: ~10 ms of SPU work a
 * frame on empty buffers. The effects belong on the GPU (docs/dev/wws-jm-post.md). */
static int s_skip;
static long s_skip_us;

static int desc_module(const uint8_t* d, uint32_t n)
{
    for (uint32_t o = 0; o + 8 <= n; o += 8)
        if ((be32(d + o) & 0xFFFF) == 0xA400)
            for (size_t i = 0; i < sizeof s_code / sizeof s_code[0]; i++)
                if (s_code[i].code_ea == be32(d + o + 4)) return s_code[i].module;
    return 0;
}

static uint32_t desc_barrier(const kz_job* j)
{
    for (uint32_t o = 0; o + 8 <= j->desc_n; o += 8) {
        const uint32_t c = be32(j->desc + o);
        if (c == 0x0800) return be32(j->desc + o + 4);
        if (c == 0x0A00) break;
    }
    return 0;
}

extern void spu_coh_pin(uint32_t ea);

static void line_written(uint32_t ea)
{
    spu_coh_notify_write(ea);
}

/* Several workers share a slot: it holds the oldest ticket they have in flight. */
#define JM_INFLIGHT 8
static uint16_t s_inflight[6][JM_INFLIGHT];   /* under the lock-line lock; 0xFFFF = none */
static void slot_set(uint32_t row, int spu)
{
    uint16_t m = 0xFFFF;
    for (int i = 0; i < JM_INFLIGHT; i++) if (s_inflight[spu][i] < m) m = s_inflight[spu][i];
    gw16(row + 4 + 2 * spu, m);
}

/* Whether the command at the ticket could be taken now, read without the lock
 * (idle workers polling under it starved everyone else of the lock). */
static int jm_peek(void)
{
    const uint32_t q = gw32(JM_HDR + 8);
    if (q >= 4) return 0;
    const uint32_t row = JM_HDR + 0x40 + 16 * q, list = gw32(JM_HDR + 0x30 + 4 * q), t = gr16(row + 2);
    if (!guest_ok(list + 8 * t, 8)) return 0;
    const uint32_t c = gw32(list + 8 * t) & 0xFFFF, ea = gw32(list + 8 * t + 4);
    return c == 1 || (c == 2 && guest_ok(ea, 2) && !gr16(ea));
}

/* Takes the next job for virtual SPU `spu`; returns the row it came from, or 0. */
static uint32_t jm_claim(int spu, kz_job* j, uint16_t* ticket)
{
    uint32_t got = 0; int changed = 0;
    spu_lockline_lock();
    const uint32_t q = gw32(JM_HDR + 8);
    const uint32_t row = JM_HDR + 0x40 + 16 * q, list = q < 4 ? gw32(JM_HDR + 0x30 + 4 * q) : 0;
    while (q < 4 && guest_ok(list, 8)) {
        const uint16_t t = gr16(row + 2);
        if (!guest_ok(list + 8 * t, 8)) break;
        const uint32_t c = gw32(list + 8 * t), ea = gw32(list + 8 * t + 4);
        if ((c & 0xFFFF) == 2) {
            if (!guest_ok(ea, 2) || gr16(ea)) break;
        } else if ((c & 0xFFFF) == 1) {
            const uint32_t n = (c >> 16) & 0xFF;
            if (n < 0x40 || n > 0xA0 || !guest_ok(ea, n)) break;
            const int m = desc_module(vm_base + ea, n);
            const int native = m && parse_desc(vm_base + ea, ea, n, m, j) && guest_ok(j->list_ea, 8 * j->list_n) && native_job(j, NULL);
            if (!native) {
                if (!s_hle_pm) break;
                memset(j, 0, sizeof *j);
                j->desc_ea = ea; j->desc_n = c;   /* module 0: the command itself in desc_n */
            }
            int i = 0;
            while (i < JM_INFLIGHT && s_inflight[spu][i] != 0xFFFF) i++;
            if (i == JM_INFLIGHT) break;
            s_inflight[spu][i] = t; *ticket = t;
            slot_set(row, spu);
            got = row;
        } else break;
        if (gr16(row) < t + 1) gw16(row, (uint16_t)(t + 1));
        gw16(row + 2, (uint16_t)(t + 1));
        changed = 1;
        if (got) break;
    }
    if (changed) line_written(JM_HDR);
    spu_lockline_unlock();
    if (changed && g_spu_line_commit_hook) g_spu_line_commit_hook(JM_HDR);
    return got;
}

/* Frees the slot; with o, first writes the native's output and does the job's barrier. */
static void jm_done(int spu, uint32_t row, uint16_t ticket, const kz_job* j, const kz_slot* o)
{
    if (o)
        for (uint32_t y = 0; y < o->rows; y++)
            memcpy(vm_base + o->out_ea + y * o->pitch, o->data + y * o->row_bytes, o->row_bytes);
    const uint32_t bar = o ? desc_barrier(j) : 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (bar && guest_ok(bar, 2)) spu_coh_pin(bar);   /* see kz_jm_init; takes the lock itself */
    spu_lockline_lock();
    if (bar && guest_ok(bar, 2)) {
        const uint16_t n = gr16(bar);
        if (n) gw16(bar, (uint16_t)(n - 1));
        line_written(bar);
    }
    for (int i = 0; i < JM_INFLIGHT; i++) if (s_inflight[spu][i] == ticket) { s_inflight[spu][i] = 0xFFFF; break; }
    slot_set(row, spu);
    line_written(JM_HDR);
    spu_lockline_unlock();
    if (g_spu_line_commit_hook) { if (bar) g_spu_line_commit_hook(bar & ~127u); g_spu_line_commit_hook(JM_HDR); }
    __atomic_add_fetch(&s_hle_jobs, 1, __ATOMIC_RELAXED);
}

/* Runs one job on a private copy of the job manager: the game's policy module on
 * its own context (virtual workload 12 + k), reading a private header whose
 * list holds just that job. The job does everything a lane does (its outputs,
 * its barrier); the private header only sees the ticket and slot. */
typedef void (*kz_entry_fn)(spu_context*);
extern kz_entry_fn spu_workload_find_img(uint64_t fingerprint, int* image_id_out);
extern int spu_run_policy_module(kz_entry_fn entry, int image_id, const uint8_t* pm_host, uint32_t pm_size,
                                 uint64_t wkl_data, uint32_t wid, uint32_t spurs_ea, uint32_t spu_num);
#define JM_PRIV 0x3F000000u
static void priv_commit(void)
{
#ifdef _WIN32
    VirtualAlloc(vm_base + JM_PRIV, 0x10000, MEM_COMMIT, PAGE_READWRITE);
#else
    mprotect(vm_base + JM_PRIV, 0x10000, PROT_READ | PROT_WRITE);
#endif
}
static int pm_run(int k, uint32_t cmd, uint32_t desc_ea)
{
    static kz_entry_fn entry; static int img = -1;
    if (img < 0) {
        entry = spu_workload_find_img(0x9A4DD8B035D2F7E5ull, &img);
        if (!entry) { fprintf(stderr, "[kz-jm] job manager image not found\n"); return 0; }
    }
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, priv_commit);
    const uint32_t H = JM_PRIV + 0x1000u * k, L = H + 0x200;
    memcpy(vm_base + H, vm_base + JM_HDR, 0x30);
    gw32w(H + 0x30, 0); gw32w(H + 0x34, 0); gw32w(H + 0x38, 0); gw32w(H + 0x3C, L);
    for (uint32_t o = 0x40; o < 0x70; o += 4) gw32w(H + o, 0x1FFFFFFF);
    gw16(H + 0x70, 1); gw16(H + 0x72, 0);
    for (int i = 0; i < 6; i++) gw16(H + 0x74 + 2 * i, 0xFFFF);
    gw32w(L, cmd); gw32w(L + 4, desc_ea); gw32w(L + 8, 0); gw32w(L + 12, 0);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const uint32_t spurs = s_jm_spurs;   /* HiPrio, the instance of jm_post */
    for (int tries = 0; tries < 1000; tries++) {
        spu_run_policy_module(entry, img, vm_base + 0x5B9180, 12160, H, 12 + k / 3, spurs, k % 3);
        if (gr16(H + 0x72) >= 1 && gr16(H + 0x74) == 0xFFFF) return 1;
    }
    fprintf(stderr, "[kz-jm] private job manager %d did not finish job %08X (ticket %u slot %04X)\n",
            k, desc_ea, gr16(H + 0x72), gr16(H + 0x74));
    return 0;
}

extern volatile int g_ps3_trace_on;
extern uint64_t ps3_trace_now(void);
extern void ps3_trace_span(const char* name, uint64_t t0_ns, uint64_t t1_ns, uint32_t arg);

/* Whether jm_gfx (wid 2: the skinned models, written into RSX local memory)
 * still has a job taken or one to take in its queue. Read without the lock: a
 * stale answer only waits a little more or a little less. */
static int gfx_busy(void)
{
    const uint32_t h = s_gfx_hdr;
    if (!h) return 0;
    const uint32_t q = gw32(h + 8);
    if (q >= 4) return 0;
    const uint32_t row = h + 0x40 + 16 * q, list = gw32(h + 0x30 + 4 * q), t = gr16(row + 2);
    for (int i = 0; i < 6; i++) if (gr16(row + 4 + 2 * i) != 0xFFFF) return 1;
    return guest_ok(list + 8 * t, 8) && (gw32(list + 8 * t) & 0xFFFF) != 0;
}

/* Whether the job at ticket t is the last of jm_post's current list (the
 * command after it ends the list): finishing it is what the PPU waits for. */
static int jm_last_of_list(uint16_t t)
{
    const uint32_t q = gw32(JM_HDR + 8);
    if (q >= 4) return 0;
    const uint32_t list = gw32(JM_HDR + 0x30 + 4 * q);
    return guest_ok(list + 8u * (t + 1u), 8) && (gw32(list + 8u * (t + 1u)) & 0xFFFF) == 0;
}

/* KZ_JM_SKIP finishes jm_post's jobs at once. On the PS3 the chain takes the
 * SPUs ~10 ms, and the game takes its end for the frame's SPU work being done:
 * in busy 144 Hz scenes the first-person weapon was then drawn before jm_gfx
 * had skinned it (the whole weapon in last frame's pose). The last job of each
 * jm_post list is finished only once jm_gfx is idle, for at most
 * KZ_JM_GFX_SYNC_US (experiment, default 0 = off; it never waited in our
 * runs, jm_gfx was always idle there). */
static void gfx_sync(void)
{
    static long us = -1;
    if (us < 0) { const char* e = getenv("KZ_JM_GFX_SYNC_US"); us = e ? atol(e) : 0; }
    if (!us || !gfx_busy()) return;
    __atomic_add_fetch(&s_gfx_waits, 1, __ATOMIC_RELAXED);
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    for (unsigned n = 0; gfx_busy(); n++) {
        if (n < 256) { _mm_pause(); continue; }
        clock_gettime(CLOCK_MONOTONIC, &b);
        if ((b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000 >= us) {
            __atomic_add_fetch(&s_gfx_timeouts, 1, __ATOMIC_RELAXED);
            return;
        }
        usleep(20);
    }
}

static void* jm_worker(void* arg)
{
    const int k = (int)(intptr_t)arg, spu = 3 + k % 3;
    kz_slot out; memset(&out, 0, sizeof out);
    kz_job j;
    unsigned idle = 0;
    for (;;) {
        uint32_t row; uint16_t t;
        if (vm_base && JM_HDR && jm_peek() && (row = jm_claim(spu, &j, &t))) {
            const uint64_t tr = g_ps3_trace_on ? ps3_trace_now() : 0;
            if (s_skip) {   /* the job's barrier and slot, no work */
                if (jm_last_of_list(t)) gfx_sync();
                if (s_skip_us) {   /* held as if it ran: see KZ_JM_SKIP_US */
                    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
                    do clock_gettime(CLOCK_MONOTONIC, &b);
                    while ((b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000 < s_skip_us);
                }
                kz_job d; const uint32_t ea = j.desc_ea, n = j.module ? j.desc_n : (j.desc_n >> 16) & 0xFF;
                if (parse_desc(vm_base + ea, ea, n, 0, &d)) { kz_slot none; memset(&none, 0, sizeof none); jm_done(spu, row, t, &d, &none); }
                else jm_done(spu, row, t, &j, NULL);
            } else
            if (j.module) { native_job(&j, &out); jm_done(spu, row, t, &j, &out); }
            else { pm_run(k, j.desc_n, j.desc_ea); jm_done(spu, row, t, &j, NULL); __atomic_add_fetch(&s_hle_pm_jobs, 1, __ATOMIC_RELAXED); }
            if (tr && g_ps3_trace_on) ps3_trace_span("jm_job", tr, ps3_trace_now(), (uint32_t)j.module);
            idle = 0;
        } else if (++idle < 256) _mm_pause();
        else usleep(50);
    }
    return NULL;
}

/* ---- statistics ---- */
static unsigned long s_cmp[16], s_bad[16], s_jobs[16], s_ns[16];
static int s_time;
static void report(void)
{
    static unsigned long n;
    if (++n % 20000) return;
    for (int m = 0; m < 16; m++) if (s_jobs[m])
        fprintf(stderr, "[kz-jm] module %d: %lu jobs, %lu rows compared, %lu mismatched, %.1f us/job\n",
                200 + m, s_jobs[m], s_cmp[m], s_bad[m], s_ns[m] / 1e3 / s_jobs[m]);
}

/* KZ_JM_CAPTURE */
static const char* s_cap_dir;
static FILE *s_cap_log, *s_cap_bin;
static unsigned s_cap_end, s_cap_jobs;
static long s_cap_off;
static int cap_on(void)
{
    extern unsigned cellGcm_flip_request_count(void);
    if (!s_cap_dir) return 0;
    const unsigned f = cellGcm_flip_request_count();
    if (!s_cap_log) {
        char b[512]; snprintf(b, sizeof b, "%s/go", s_cap_dir);
        static unsigned last;
        if (f == last || access(b, F_OK)) { last = f; return 0; }
        pthread_mutex_lock(&s_mu);
        if (!s_cap_log) {
            snprintf(b, sizeof b, "%s/dma.bin", s_cap_dir); s_cap_bin = fopen(b, "wb");
            snprintf(b, sizeof b, "%s/dma.log", s_cap_dir); s_cap_log = fopen(b, "w");
            s_cap_end = f + 3;
        }
        pthread_mutex_unlock(&s_mu);
    }
    if (f < s_cap_end) return 1;
    pthread_mutex_lock(&s_mu);
    if (s_cap_bin) { fclose(s_cap_bin); fclose(s_cap_log); s_cap_bin = NULL; s_cap_dir = NULL; fprintf(stderr, "[kz-jm] capture done\n"); }
    pthread_mutex_unlock(&s_mu);
    return 0;
}
static void cap_dma(spu_context* ctx, uint32_t ea, uint32_t lsa, uint32_t size, int put)
{
    pthread_mutex_lock(&s_mu);
    if (s_cap_bin) {
        fprintf(s_cap_log, "%p %c %08X %05X %X %lX\n", (void*)ctx, put ? 'P' : 'G', ea, lsa & SPU_LS_MASK, size, s_cap_off);
        fwrite(put ? ctx->ls + (lsa & SPU_LS_MASK) : vm_base + ea, 1, size, s_cap_bin);
        s_cap_off += size;
    }
    pthread_mutex_unlock(&s_mu);
}
static void cap_job(spu_context* ctx, int module)
{
    pthread_mutex_lock(&s_mu);
    if (s_cap_bin) {
        char b[512]; snprintf(b, sizeof b, "%s/job%03u.bin", s_cap_dir, s_cap_jobs);
        FILE* f = fopen(b, "wb");
        if (f) { fwrite(ctx->gpr, 16, 128, f); fwrite(ctx->ls, 1, SPU_LS_SIZE, f); fclose(f); }
        fprintf(s_cap_log, "%p J %d %u\n", (void*)ctx, module, s_cap_jobs++);
    }
    pthread_mutex_unlock(&s_mu);
}

static void dma_hook(spu_context* ctx, uint32_t ea, uint32_t lsa, uint32_t size, int put)
{
    if (!ctx->policy_mode || ctx->ls[0x1DF] != 1) return;   /* workload 1 only */
    if (s_cap_dir && cap_on()) cap_dma(ctx, ea, lsa, size, put);
    kz_slot* s = slot_of(ctx);
    if (!s) return;
    if (!put && guest_ok(ea, size) && size >= 0x70 && size <= 0xA0 && !(size & 15)) {
        s->desc_ea = ea; s->desc_n = size; memcpy(s->desc, vm_base + ea, size);
        return;
    }
    if (put && s->active && ea >= s->out_ea && size <= s->row_bytes) {
        const uint32_t off = ea - s->out_ea, r = off / s->pitch, c = off % s->pitch;
        if (r < s->rows && c + size <= s->row_bytes) {
            const int m = s->module - 200;
            __atomic_add_fetch(&s_cmp[m], 1, __ATOMIC_RELAXED);
            if (memcmp(ctx->ls + (lsa & SPU_LS_MASK), s->data + r * s->row_bytes + c, size)) {
                unsigned long b = __atomic_add_fetch(&s_bad[m], 1, __ATOMIC_RELAXED);
                if (b <= 8) {
                    fprintf(stderr, "[kz-jm] module %d MISMATCH row %u col %u (job desc 0x%08X)", s->module, r, c, s->desc_ea);
                    kz_job j; parse_job(s, s->module, &j);
                    for (int i = 16; i < 32; i += 4) fprintf(stderr, " %08X", be32(j.param + i));
                    fprintf(stderr, " list");
                    for (uint32_t i = j.list_n > 8 ? j.list_n - 8 : 0; i < j.list_n; i++) fprintf(stderr, " %08X", gw32(j.list_ea + 8 * i + 4));
                    const uint8_t* g = ctx->ls + (lsa & SPU_LS_MASK); const uint8_t* n = s->data + r * s->row_bytes + c;
                    fprintf(stderr, " spu %02X%02X%02X%02X nat %02X%02X%02X%02X\n", g[0], g[1], g[2], g[3], n[0], n[1], n[2], n[3]);
                }
            }
        }
    }
}

static void job_hook(spu_context* ctx)
{
    if (ctx->ls[0x1DF] != 1) return;
    kz_slot* s = slot_of(ctx);
    if (!s || !s->desc_n) return;
    kz_job j;
    const int module = ctx->resident_ovl;
    if (s_cap_dir && (module == 203 || module == 206) && cap_on()) cap_job(ctx, module);
    if (!parse_job(s, module, &j)) return;
    if (module >= 200 && module < 216) __atomic_add_fetch(&s_jobs[module - 200], 1, __ATOMIC_RELAXED);
    extern unsigned cellGcm_flip_request_count(void);
    static unsigned from = 0xFFFFFFFFu; static unsigned long logged;
    if (from == 0xFFFFFFFFu) { const char* e = getenv("KZ_JM_LOG_FROM"); from = e ? (unsigned)atoi(e) : 0; }
    if (s_log && cellGcm_flip_request_count() >= from && logged < 4000) {
        pthread_mutex_lock(&s_mu); logged++;
        fprintf(s_log, "%d desc=%08X list=%08X/%u", module, j.desc_ea, j.list_ea, j.list_n);
        for (int i = 0; i < 48; i += 4) fprintf(s_log, "%s%08X", i % 16 ? " " : " | ", be32(j.param + i));
        if (j.list_n) fprintf(s_log, " | first %08X:%08X last %08X:%08X", gw32(j.list_ea), gw32(j.list_ea + 4),
                              gw32(j.list_ea + 8 * (j.list_n - 1)), gw32(j.list_ea + 8 * (j.list_n - 1) + 4));
        for (uint32_t o = 0; o + 8 <= j.desc_n; o += 8) {
            const uint32_t c = be32(j.desc + o), ea = be32(j.desc + o + 4);
            if ((c & 0xFFFF) == 0xC440 || (c & 0xFFFF) == 0xC460) {
                const uint32_t n = (c >> 16) / 2;
                fprintf(s_log, " | list %08X/%u:", ea, n);
                for (uint32_t i = 0; i < n && i < 128; i++) fprintf(s_log, " %X:%08X", gw32(ea + 8 * i), gw32(ea + 8 * i + 4));
            }
        }
        fprintf(s_log, " | desc");
        for (uint32_t o = 0; o < j.desc_n; o += 4) fprintf(s_log, " %08X", be32(j.desc + o));
        fputc('\n', s_log);
        pthread_mutex_unlock(&s_mu);
    }
    if (s_time) {
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        const uint64_t now = ts.tv_sec * 1000000000ull + ts.tv_nsec;
        if (s->t0 && now - s->t0 < 2000000) __atomic_add_fetch(&s_ns[s->t_module - 200], now - s->t0, __ATOMIC_RELAXED);
        s->t0 = now; s->t_module = module;
    }
    s->active = 0;
    if (s_verify && native_job(&j, s)) { s->module = module; s->active = 1; }
    report();
}

/* KZ_JM_WATCH: the job manager's header every second (hang diagnosis) */
static void* watch(void* arg)
{
    (void)arg;
    extern unsigned cellGcm_flip_request_count(void);
    for (;;) {
        sleep(1);
        if (!vm_base) continue;
        { static int done; const char* e = getenv("KZ_JM_DUMPIMG");
          if (e && !done && cellGcm_flip_request_count() >= (unsigned)atoi(e)) {
              done = 1;
              const uint32_t bufs[][4] = { {0x404D2000u,1280,720,0x1400}, {0x40856000u,640,360,0xA00}, {0x40937000u,640,360,0xA00},
                                           {0x40A18000u,640,360,0xA00}, {0x40E7D000u,320,180,0x500} };
              for (int b = 0; b < 5; b++) {
                  char path[128]; snprintf(path, sizeof path, "/tmp/claude-1000/jmimg_%08X.ppm", bufs[b][0]);
                  FILE* f = fopen(path, "wb"); if (!f) continue;
                  fprintf(f, "P6\n%u %u\n255\n", bufs[b][1], bufs[b][2]);
                  for (uint32_t y = 0; y < bufs[b][2]; y++) for (uint32_t x = 0; x < bufs[b][1]; x++) {
                      const uint8_t* q = vm_base + bufs[b][0] + y * bufs[b][3] + 4 * x; fputc(q[1], f); fputc(q[2], f); fputc(q[3], f); }
                  fclose(f);
              } } }
        fprintf(stderr, "[kz-jmw] flips=%u hle=%lu pm=%lu gfx_waits=%lu gfx_timeouts=%lu", cellGcm_flip_request_count(), s_hle_jobs, s_hle_pm_jobs, s_gfx_waits, s_gfx_timeouts);
        for (uint32_t o = 0; o < 0x100; o += 4) fprintf(stderr, "%s%08X", o % 16 ? " " : " | ", gw32(JM_HDR + o));
        fputc('\n', stderr);
        if (JM_HDR) {   /* the command at the current queue's ticket, and a barrier's count */
            const uint32_t q = gw32(JM_HDR + 8);
            if (q < 4) {
                const uint32_t row = JM_HDR + 0x40 + 16 * q, list = gw32(JM_HDR + 0x30 + 4 * q), t = gr16(row + 2);
                const uint32_t c = guest_ok(list + 8 * t, 8) ? gw32(list + 8 * t) : 0, ea = guest_ok(list + 8 * t, 8) ? gw32(list + 8 * t + 4) : 0;
                fprintf(stderr, "[kz-jmw] q=%u t=%u cmd=%08X ea=%08X%s%04X\n", q, t, c, ea,
                        (c & 0xFFFF) == 2 ? " barrier count=" : " ", (c & 0xFFFF) == 2 && guest_ok(ea, 2) ? gr16(ea) : 0);
            }
        }
    }
    return NULL;
}

extern void (*g_spurs_workload_added_hook)(uint32_t spurs_ea, uint32_t wid, const char* name_class,
                                           const char* name_instance, uint64_t data);
static void jm_found(uint32_t spurs_ea, uint32_t wid, const char* cls, const char* inst, uint64_t data)
{
    (void)wid; (void)cls;
    if (!strcmp(inst, "jm_gfx")) __atomic_store_n(&s_gfx_hdr, (uint32_t)data, __ATOMIC_RELEASE);
    if (strcmp(inst, "jm_post") || s_jm_hdr) return;
    s_jm_spurs = spurs_ea;
    spu_coh_pin((uint32_t)data);   /* see kz_jm_init; takes the lock itself */
    __atomic_store_n(&s_jm_hdr, (uint32_t)data, __ATOMIC_RELEASE);
    fprintf(stderr, "[kz-jm] jm_post: header 0x%08X, instance 0x%08X\n", (uint32_t)data, spurs_ea);
}

__attribute__((constructor)) static void kz_jm_init(void)
{
    /* KZ_MLAA: where jm_post's MLAA (the only post effect in normal play) comes
     * from. gpu (default): the renderer runs it on the display buffer where the
     * game copies its frame to the SPUs (ps3recomp PS3RECOMP_MLAA_GPU, same
     * rules as native/mlaa_ref.c); spu: the game's own SPU chain, with the
     * render targets kept coherent and the NV0039 copies done (exact, slow);
     * off: none. Glow and motion blur only exist on the spu path. */
    { const char* m = getenv("KZ_MLAA");
      if (m && !strcmp(m, "spu")) {
          setenv("KZ_JM_SKIP", "0", 0); setenv("PS3RECOMP_ENG_MAIN_COHERENCE", "1", 0); setenv("PS3RECOMP_NV0039", "1", 0);
      } else if (!m || strcmp(m, "off")) setenv("PS3RECOMP_MLAA_GPU", "1", 0); }
    { const char* e = getenv("KZ_JM_HLE_PM"); s_hle_pm = !e || atoi(e); }
    { const char* e = getenv("KZ_JM_SKIP"); s_skip = !e || atoi(e); }
    { const char* e = getenv("KZ_JM_SKIP_US"); s_skip_us = e ? atol(e) : 0; }
    { const char* e = getenv("KZ_JM_HLE"); int n = e ? atoi(e) : s_skip ? 1 : 3;
      memset(s_inflight, 0xFF, sizeof s_inflight);
      /* The workers update the header (and the barriers, below) under the
       * lock-line lock with plain stores; pinned, the game's own PPU stores
       * and store-conditionals to those lines take the same lock. Unpinned,
       * no SPU reserves the header (the lanes do not run jm_post), and a PPU
       * update of the queue landing inside a worker's read-modify-write was
       * lost: the ticket fell behind the base, no job was ever taken again,
       * and the main thread waited on jm_post for good (144 Hz clock,
       * minutes into a firefight). */
      if (n > 0) g_spurs_workload_added_hook = jm_found;
      for (int i = 0; i < n && i < 12; i++) { pthread_t t; pthread_create(&t, NULL, jm_worker, (void*)(intptr_t)i); } }
    if (getenv("KZ_JM_WATCH")) { pthread_t t; pthread_create(&t, NULL, watch, NULL); }
    const char* l = getenv("KZ_JM_LOG");
    s_verify = getenv("KZ_JM_VERIFY") != NULL;
    s_time = getenv("KZ_JM_TIME") != NULL;
    if (l) s_log = fopen(l, "w");
    s_cap_dir = getenv("KZ_JM_CAPTURE");
    if (s_log || s_verify || s_time || s_cap_dir) { g_spu_dma_hook = dma_hook; g_spu_job_hook = job_hook; }
}
