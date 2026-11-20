/* The SPURS lock-free queue without the firmware's libsre (ps3recomp patch 1022, libs/spurs/cellSpursLFQueue.c).
 *
 * 1. _cellSpursLFQueuePushBody on the 103 lines in test_lfqueue_vectors.txt: return code, the 128-byte line after it
 *    and the wake-ups handed out must be what the firmware 4.93 libsre gave on the same line (recorded with the
 *    differential harness described in docs/dev/port-journal.md; 42,000 random cases matched there).
 * 2. _cellSpursLFQueueInitialize as Killzone HD calls it (func_003A84F0): the bytes libsre writes, and its checks.
 * 3. A blocking push into a full queue waits until a pop makes room, then lands (libsre slept on an lv2 event queue
 *    this runtime never creates; the native push tries again instead).
 * 4. A wake-up for a queue owned by a SPURS instance (eaSignal = spurs | 1) reaches the taskset of that workload.
 *
 * Built by test_lfqueue.sh against the patched tree. Exit 0 on pass, 1 on a failure. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ppu/ppu_context.h"
#include "ppu/ppu_memory.h"

static uint8_t g_mem[0x02100000] __attribute__((aligned(128)));   /* the vectors' element buffer is at 0x020C2000 */
uint8_t* vm_base = g_mem;
uint32_t ppu_vm_size = sizeof g_mem;
PPU_THREAD_LOCAL ppu_context* g_active_ctx;
uint32_t g_ps3_sdk_version = 0x00350001;

/* The guest's reservation primitives, reduced to compare-and-swap on the big-endian value. */
void ppu_resv_snap(uint64_t ea) { (void)ea; }
int ppu_stwcx32(uint64_t ea, uint32_t expected, uint32_t val)
{
    uint32_t e = __builtin_bswap32(expected), n = __builtin_bswap32(val);
    return __atomic_compare_exchange_n((uint32_t*)(g_mem + ea), &e, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int ppu_stdcx64(uint64_t ea, uint64_t expected, uint64_t val)
{
    uint64_t e = __builtin_bswap64(expected), n = __builtin_bswap64(val);
    return __atomic_compare_exchange_n((uint64_t*)(g_mem + ea), &e, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t a) { (void)a; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int  spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
void spu_coh_notify_write(uint32_t a) { (void)a; }

static uint32_t g_nids[4]; static int g_nnids;
void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*))
{
    (void)name; (void)fn;
    if (g_nnids < 4) g_nids[g_nnids++] = nid;
}
uint32_t cellSpursHleTasksetByWid(uint32_t spurs, uint32_t wid) { return spurs == 0x10F00000u && wid == 2 ? 0x10E00000u : 0; }
static uint32_t g_hle_sig[2];
int32_t _cellSpursSendSignal(void* taskset, uint32_t task)
{
    g_hle_sig[0] = (uint32_t)(uintptr_t)taskset; g_hle_sig[1] = task;
    return 0;
}

#include "spurs/cellSpursLFQueue.c"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_fail = 1; } } while (0)

static uint32_t g_sig[64][2]; static unsigned g_nsig, g_sig_fail_at;
static int32_t record_sig(uint32_t ea, uint32_t id)
{
    if (g_nsig < 64) { g_sig[g_nsig][0] = ea; g_sig[g_nsig][1] = id; }
    g_nsig++;
    return g_sig_fail_at && g_nsig == g_sig_fail_at ? (int32_t)0x80410905 : 0;
}

static void hex_in(uint8_t* dst, const char* s)
{
    for (int i = 0; i < 128; i++) { unsigned v; sscanf(s + 2 * i, "%2x", &v); dst[i] = (uint8_t)v; }
}

#define Q    0x1000u   /* the queue line */
#define DATA 0x2000u   /* the element pushed */

static void vectors(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) { printf("FAIL: no %s\n", path); exit(1); }
    char line[4096], before[300], after[300], kind[8];
    unsigned blk, failat, ret, nsig, n = 0;
    ppu_context ctx; memset(&ctx, 0, sizeof ctx);
    g_active_ctx = &ctx;
    cellSpursLFQueueHleSignal = record_sig;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        int used;
        if (sscanf(line, "%7s %u %x %256s %x %256s %u%n", kind, &blk, &failat, before, &ret, after, &nsig, &used) != 7) {
            printf("FAIL: bad vector line %u\n", n); exit(1);
        }
        uint8_t want[128];
        hex_in(g_mem + Q, before);
        hex_in(want, after);
        for (unsigned i = 0; i < 64; i++) g_mem[DATA + i] = (uint8_t)(i * 7 + n);
        g_nsig = 0; g_sig_fail_at = failat;
        const uint32_t r = cellSpursLFQueueHlePushBody(&ctx, Q, DATA, blk);
        CHECK(r == ret, "vector %u: result 0x%08X, libsre 0x%08X", n, r, ret);
        CHECK(!memcmp(g_mem + Q, want, 128), "vector %u: the line differs from libsre's", n);
        CHECK(g_nsig == nsig, "vector %u: %u wake-ups, libsre %u", n, g_nsig, nsig);
        const char* p = line + used;
        for (unsigned i = 0; i < nsig && i < g_nsig; i++) {
            unsigned ea, id; int k;
            if (sscanf(p, " %x:%x%n", &ea, &id, &k) != 2) { printf("FAIL: vector %u: bad wake-up\n", n); exit(1); }
            p += k;
            CHECK(g_sig[i][0] == ea && g_sig[i][1] == id, "vector %u: wake-up %u is %08X:%04X, libsre %08X:%04X", n, i,
                  g_sig[i][0], g_sig[i][1], ea, id);
        }
        n++;
    }
    fclose(f);
    CHECK(n == 103, "%u vectors read, 103 expected", n);
    printf("vectors: %u pushes as libsre answered them\n", n);
}

static void init_as_the_game(void)
{
    ppu_context ctx; memset(&ctx, 0, sizeof ctx);
    g_active_ctx = &ctx;
    memset(g_mem + Q, 0, 0x80 + 8 * 0x20);
    const uint32_t owner = 0x10F00000u | 1, depth = 8;
    uint32_t r = cellSpursLFQueueHleInitialize(&ctx, owner, Q, Q + 0x80, 0x20, depth, 3);
    CHECK(r == 0, "init: 0x%08X", r);
    static const uint8_t want[128] = {
        [0x13] = 0x20, [0x17] = 8, [0x1E] = 0x10, [0x1F] = 0x81, [0x20] = 0xFF, [0x21] = 0xFF, [0x27] = 3,
        [0x28] = 0xFF, [0x29] = 0xFF, [0x2A] = 0xFF, [0x2B] = 0xFF, [0x2F] = 2, [0x30] = 0xFF, [0x31] = 0xFF,
        [0x50] = 0xFF, [0x51] = 0xFF, [0x74] = 0x10, [0x75] = 0xF0, [0x77] = 0x01,
    };
    CHECK(!memcmp(g_mem + Q, want, 128), "init: the line is not the one libsre writes");
    /* The same call again: libsre compares the buffer with the stored one, which carries the ANY2ANY bit. */
    r = cellSpursLFQueueHleInitialize(&ctx, owner, Q, Q + 0x80, 0x20, depth, 3);
    CHECK(r == 0x80410902u, "init twice: 0x%08X, libsre 0x80410902", r);
    CHECK(cellSpursLFQueueHleInitialize(&ctx, owner, Q + 0x40, Q + 0x80, 0x20, depth, 3) == 0x80410910u, "misaligned");
    CHECK(cellSpursLFQueueHleInitialize(&ctx, owner, Q, Q + 0x80, 0x18, depth, 3) == 0x80410902u, "size % 16");
    CHECK(cellSpursLFQueueHleInitialize(&ctx, owner, 0, Q + 0x80, 0x20, depth, 3) == 0x80410911u, "null queue");
    printf("init: the line Killzone HD's queue gets\n");
}

static atomic_int g_popped;
static void* popper(void* arg)
{
    (void)arg;
    usleep(50000);
    /* an SPU consumer finishing one pop: pops reserved and committed move by one */
    vm_write16(Q + 0x06, (uint16_t)(vm_read16(Q + 0x06) + 1));
    vm_write16(Q + 0x00, (uint16_t)(vm_read16(Q + 0x00) + 1));
    atomic_store(&g_popped, 1);
    return NULL;
}

static void blocking_push(void)
{
    ppu_context ctx; memset(&ctx, 0, sizeof ctx);
    g_active_ctx = &ctx;
    memset(g_mem + Q, 0, 0x80 + 4 * 0x20);
    CHECK(cellSpursLFQueueHleInitialize(&ctx, 0x10F00001u, Q, Q + 0x80, 0x20, 4, 3) == 0, "init");
    cellSpursLFQueueHleSignal = record_sig; g_sig_fail_at = 0;
    for (unsigned i = 0; i < 4; i++) CHECK(cellSpursLFQueueHlePushBody(&ctx, Q, DATA, 3) == 0, "push %u", i);
    CHECK(cellSpursLFQueueHlePushBody(&ctx, Q, DATA, 0) == 0x80410901u, "full, not blocking: AGAIN");
    pthread_t t; pthread_create(&t, NULL, popper, NULL);
    for (unsigned i = 0; i < 32; i++) g_mem[DATA + i] = (uint8_t)(0xA0 + i);
    const uint32_t r = cellSpursLFQueueHlePushBody(&ctx, Q, DATA, 3);
    pthread_join(t, NULL);
    CHECK(atomic_load(&g_popped) && r == 0, "blocking push into a full queue: 0x%08X before the pop", r);
    CHECK(!memcmp(g_mem + Q + 0x80, g_mem + DATA, 32), "the fifth element is in slot 0");
    CHECK(vm_read16(Q + 0x08) == 5 && vm_read16(Q + 0x0E) == 5, "pushes committed/reserved 5/5");
    printf("blocking push: waited for the pop, then landed\n");
}

static void spurs_owned_signal(void)
{
    cellSpursLFQueueHleSignal = lfq_send_signal;
    g_hle_sig[0] = g_hle_sig[1] = 0;
    CHECK(cellSpursLFQueueHleSignal(0x10F00001u, 0x0203) == 0 && g_hle_sig[0] == 0x10E00000u && g_hle_sig[1] == 3,
          "spurs | 1, wid 2 task 3 -> taskset 0x10E00000 task 3 (got 0x%08X %u)", g_hle_sig[0], g_hle_sig[1]);
    CHECK(cellSpursLFQueueHleSignal(0x10F00001u, 0x0503) == (int32_t)0x80410905, "an unknown workload: SRCH");
    CHECK(cellSpursLFQueueHleSignal(0x10D00000u, 7) == 0 && g_hle_sig[0] == 0x10D00000u && g_hle_sig[1] == 7,
          "a taskset owner is signalled as it is");
    printf("wake-ups: a SPURS-owned queue reaches its taskset\n");
}

int main(int argc, char** argv)
{
    if (argc != 2) { printf("usage: %s <vectors>\n", argv[0]); return 1; }
    cellSpursLFQueue_register_ctx_handlers();
    CHECK(g_nnids == 2 && g_nids[0] == 0x011EE38Bu && g_nids[1] == 0x8A85674Du, "both imports registered");
    vectors(argv[1]);
    init_as_the_game();
    blocking_push();
    spurs_owned_signal();
    printf(g_fail ? "FAILED\n" : "PASS\n");
    return g_fail;
}
