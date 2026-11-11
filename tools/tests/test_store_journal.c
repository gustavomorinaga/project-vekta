/* The guest store journal (patch 3034, runtime/ppu/ppu_store_journal.h, built with PS3RECOMP_STORE_JOURNAL): stores
 * noted before they land can be undone exactly, newest first, also when they overlap; a journal that runs out of room
 * says so; another thread's stores are not noted but counted; beginning a journal closes the store window and holds the global
 * flag the store paths test until it ends; and nothing is noted while no thread journals. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppu_store_journal.h"

/* What ppu_loader.cpp defines in the runtime. */
uint8_t* vm_base;
volatile uint32_t g_store_journal;
volatile uint64_t g_store_journal_foreign;
_Thread_local ppu_store_journal* t_store_journal;
static int s_closed;
void ppu_journal_window_close(void) { s_closed++; }

static int fails;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

/* a guest store as the runtime makes it: noted, then written */
static void store(uint32_t ea, const void* src, uint32_t n)
{
    PPU_JOURNAL_NOTE(ea, n);
    memcpy(vm_base + ea, src, n);
}

static uint32_t t_ea[64], t_n[64], t_off[64];
static uint8_t t_old[1024];
static ppu_store_journal mk(uint32_t cap, uint32_t old_cap)
{
    ppu_store_journal j = { t_ea, t_n, t_off, cap, 0, t_old, old_cap, 0, 0 };
    return j;
}

static void* other_thread(void* arg)
{
    (void)arg;
    const uint32_t v = 0x11111111u;
    store(0x300, &v, 4);   /* while the main thread journals: written, not noted */
    return NULL;
}

int main(void)
{
    vm_base = calloc(1, 0x10000);
    for (int i = 0; i < 0x1000; i++) vm_base[i] = (uint8_t)(i * 7);
    uint8_t before[0x1000];
    memcpy(before, vm_base, sizeof before);

    const uint32_t w = 0xDEADBEEFu;
    store(0x100, &w, 4);
    CHECK(g_store_journal == 0 && t_store_journal == NULL, "no journal: a store is written and nothing is noted");
    memcpy(vm_base + 0x100, before + 0x100, 4);

    ppu_store_journal j = mk(64, sizeof t_old);
    ppu_journal_begin(&j);
    CHECK(g_store_journal == 1 && s_closed == 1, "begin holds the global flag and closes the store window");
    const uint64_t d = 0x0102030405060708ull;
    uint8_t line[128];
    memset(line, 0, sizeof line);
    store(0x100, &w, 4);
    store(0x102, &d, 8);            /* overlaps the first */
    store(0x180, line, 128);        /* a dcbz-sized line */
    store(0x100, &d, 8);            /* the same bytes again */
    pthread_t th;
    pthread_create(&th, NULL, other_thread, NULL);
    pthread_join(th, NULL);
    CHECK(j.count == 4 && j.used == 4 + 8 + 128 + 8 && !j.overflow, "four stores noted with their old bytes");
    CHECK(ppu_journal_find(&j, 0x300, 4) < 0, "the other thread's store is not in this thread's journal");
    CHECK(g_store_journal_foreign == 1, "the other thread's store is counted as a foreign store");
    CHECK(ppu_journal_find(&j, 0x105, 1) == 1 && ppu_journal_find(&j, 0x1FF, 1) == 2 && ppu_journal_find(&j, 0x200, 4) < 0,
          "find: the first entry that touches a range, none past the line");
    ppu_journal_end();
    CHECK(g_store_journal == 0 && t_store_journal == NULL, "end releases the flag");
    ppu_journal_undo(&j);
    memcpy(vm_base + 0x300, before + 0x300, 4);   /* the other thread's store is not ours to undo */
    CHECK(!memcmp(vm_base, before, sizeof before), "undo puts every byte back, overlapping stores included");

    ppu_store_journal k = mk(2, sizeof t_old);
    ppu_journal_begin(&k);
    store(0x10, &w, 4); store(0x20, &w, 4); store(0x30, &w, 4);
    ppu_journal_end();
    CHECK(k.overflow && k.count == 2, "a third store into a two-entry journal sets overflow");
    ppu_store_journal m = mk(64, 6);
    ppu_journal_begin(&m);
    store(0x40, &w, 4); store(0x44, &w, 4);
    ppu_journal_end();
    CHECK(m.overflow && m.count == 1, "old bytes past the journal's room set overflow too");

    free(vm_base);
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    return 0;
}
