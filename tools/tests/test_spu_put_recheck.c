/* A plain SPU PUT racing another SPU's GETLLAR of a line it covers (patch 1020).
 *
 * The PUT scans its span for reserved lines before copying, and copies an
 * unreserved span without the lock-line lock. A GETLLAR can land between the
 * scan and the end of the copy: its snapshot may be old or torn, and unless the
 * PUT checks the span again after the copy and ends that reservation, a PUTLLC
 * against it later commits over a line its SPU never saw whole. mfc_do_transfer
 * has always re-checked; this tests the fast path patch 1010 added, which is
 * what runs with no DMA diagnostic set.
 *
 * Each round: no reservation on the line, then the writer PUTs 16 KiB of a new
 * byte ending with the line (the line is copied last, so the window is the
 * whole copy) while the reader GETLLARs the line at a varying delay. After
 * both, the reader's reservation must be gone, or its snapshot must be the new
 * line throughout.
 *
 * Built against the patched tree by test_spu_put_recheck.sh (spu_channels.c
 * compiled in, as ps3recomp's own test_spu_lockline_peer.c does). Exit 0 on
 * pass, 1 on a failure, 2 if no round overlapped (nothing tested). */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t g_guest_mem[1u << 20];
uint8_t* vm_base = g_guest_mem;
uint32_t ppu_vm_size = 0;

#include "spu_channels.c"

#define SPAN     0x4000u
#define LINE     0x00040000u               /* the reserved line, the span's last */
#define SPAN_EA  (LINE + 0x80u - SPAN)
#define LSA_R    0x00001000u
#define LSA_W    0x00010000u
#define ROUNDS   200000

static spu_context g_r, g_w;   /* reader, writer: 256 KB of local store each */
static atomic_int g_go, g_r_done, g_w_done;

static void wrch(spu_context* ctx, uint32_t channel, uint32_t v)
{
    spu_wrch(ctx, channel, spu_make_preferred_u32(v));
}

static void cmd(spu_context* ctx, uint32_t lsa, uint32_t ea, uint32_t size, uint32_t c)
{
    wrch(ctx, MFC_LSA, lsa);
    wrch(ctx, MFC_EAH, 0);
    wrch(ctx, MFC_EAL, ea);
    if (size) wrch(ctx, MFC_Size, size);
    wrch(ctx, MFC_TagID, 0);
    wrch(ctx, MFC_Cmd, c);
}

static void delay(unsigned n) { for (unsigned i = 0; i < n; i++) YieldProcessor(); }

static void* reader(void* arg)
{
    (void)arg;
    for (int k = 1; k <= ROUNDS; k++) {
        while (atomic_load(&g_go) != k) { }
        delay((unsigned)(k * 7) & 255u);
        cmd(&g_r, LSA_R, LINE, 0, MFC_GETLLAR_CMD);
        atomic_store(&g_r_done, k);
    }
    return NULL;
}

static void* writer(void* arg)
{
    (void)arg;
    for (int k = 1; k <= ROUNDS; k++) {
        while (atomic_load(&g_go) != k) { }
        cmd(&g_w, LSA_W, SPAN_EA, SPAN, MFC_PUT_CMD);
        atomic_store(&g_w_done, k);
    }
    return NULL;
}

/* The runtime beyond the SPU channel layer, stubbed as test_spu_lockline_peer.c stubs it: none of it is on the
 * path this test drives. */
void ppu_resv_break_store(uint64_t ea) { (void)ea; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
unsigned long long ps3_ms_now(void) { return 0; }
uint32_t g_ww_lo = 0xFFFFFFFFu, g_ww_hi = 0;
int g_resv_store_active = 0;
uint32_t g_barrier_sync_watch = 0;
void (*g_spurs_kernel_hook)(uint32_t) = 0;
uint32_t g_ydkj_spurs_ctx_ea, g_ydkj_real_spurs_ea, g_ydkj_real_taskset_ea, g_ydkj_real_taskid;
void spurs_ef_set_from_spu(uint32_t ea, uint16_t bits) { (void)ea; (void)bits; }
void ydkj_wake_all_event_flags(void) {}
void spurs_pm_build_context(spu_context* c, uint32_t a, uint32_t b, uint32_t d) { (void)c; (void)a; (void)b; (void)d; }
uint32_t g_spu_image_src_ea, g_spu_image_ls_start, g_spu_image_span;
void spu_thread_publish_ctx(uint32_t tid, void* ctx) { (void)tid; (void)ctx; }

int main(void)
{
    spu_context_init(&g_r, 0); g_r.spu_id = 0;
    spu_context_init(&g_w, 0); g_w.spu_id = 1;
    pthread_t r, w;
    if (pthread_create(&r, NULL, reader, NULL) || pthread_create(&w, NULL, writer, NULL)) {
        fprintf(stderr, "FAIL: threads did not start\n");
        return 1;
    }
    unsigned long overlapped = 0, ended = 0, stale_kept = 0;
    for (int k = 1; k <= ROUNDS; k++) {
        const uint8_t v = (uint8_t)(k % 255 + 1);
        /* No reservation left from the last round: end it as a PPU store does
         * (which also drops the line's mark), so the PUT's scan finds none. */
        spu_lockline_lock();
        spu_coh_notify_write(LINE);
        spu_lockline_unlock();
        g_r.event_status = 0;
        memset(g_w.ls + LSA_W, v, SPAN);
        atomic_store(&g_go, k);
        while (atomic_load(&g_r_done) != k || atomic_load(&g_w_done) != k) { }
        int whole = 1;
        for (int i = 0; i < 128; i++)
            if (g_r.resv_line[i] != v) { whole = 0; break; }
        if (!whole) overlapped++;   /* the snapshot predates the end of the copy */
        if (!g_r.resv_valid) ended++;
        else if (!whole) stale_kept++;
    }
    pthread_join(r, NULL);
    pthread_join(w, NULL);
    printf("PUT vs GETLLAR: %d rounds, %lu snapshots before the copy ended, %lu reservations ended, "
           "%lu kept with a stale snapshot\n", ROUNDS, overlapped, ended, stale_kept);
    if (stale_kept) {
        fprintf(stderr, "FAIL: a reservation taken during an unreserved PUT survived with a stale snapshot\n");
        return 1;
    }
    if (!overlapped) {
        fprintf(stderr, "FAIL: no GETLLAR landed inside a PUT, so nothing was tested\n");
        return 2;
    }
    puts("PASS: a GETLLAR inside an unreserved PUT ends with the new line or no reservation");
    return 0;
}
