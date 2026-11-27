/* The sceNpTrophy host API (patch 5002) on a synthetic trophy set: the notices
 * and their order (the platinum last), no repeat, the language's names, the
 * store surviving a reload (written through a temp file), the debug block, the
 * icons. Built by test_trophy_notify.sh against the patched tree; no game data.
 * The stubs are the ones libs/network/tests/test_trophy_registration.c uses. */
#include "sceNpTrophy.c"
#undef NDEBUG
#include <assert.h>
#include <dirent.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
const char* ppu_vfs_root;
void ppu_resv_break_store(uint64_t ea) { (void)ea; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_coh_notify_write(uint32_t a) { (void)a; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
void cellMsgDialog_tick(void) {}
void np_psnr_pump(void) {}
void ps3_np_eula_tick(void) {}

static void status_cb(u32 opd, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, u64 a7)
{
    (void)opd; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
}

/* what the notify callback heard */
static int heard_id[16], heard_grade[16], heard_n;
static char heard_name[16][64], heard_comm[16][16];
static void on_notice(const ps3_trophy_notice* n, void* user)
{
    assert(user == (void*)0x1234 && heard_n < 16);
    heard_id[heard_n] = n->id;
    heard_grade[heard_n] = n->grade;
    snprintf(heard_name[heard_n], sizeof heard_name[0], "%s", n->name);
    snprintf(heard_comm[heard_n], sizeof heard_comm[0], "%s", n->comm_id);
    /* the callback may query: the store's lock is not held */
    ps3_trophy_def d;
    assert(ps3_trophy_get(n->id, &d) == 0 && d.unlocked);
    heard_n++;
}

/* A TRP: magic, entry count at +0x10, entry size 0x40 at +0x14, entries from
 * +0x40 (name[32], u64 offset, u64 size), data after them. */
static void put32(u8* p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void put64(u8* p, u64 v) { put32(p, (u32)(v >> 32)); put32(p + 4, (u32)v); }
static const char CONF[] =
    "<trophyconf version=\"1.1\"><title-name>Test Set</title-name>\n"
    "<trophy id=\"000\" hidden=\"no\" ttype=\"P\" pid=\"-1\"></trophy>\n"
    "<trophy id=\"001\" hidden=\"no\" ttype=\"B\" pid=\"000\"></trophy>\n"
    "<trophy id=\"002\" hidden=\"yes\" ttype=\"S\" pid=\"000\"></trophy>\n</trophyconf>\n";
static const char EN[] =
    "<trophyconf><title-name>Test Set</title-name>\n"
    "<trophy id=\"000\"><name>Plat</name><detail>All of them</detail></trophy>\n"
    "<trophy id=\"001\"><name>First</name><detail>Do one</detail></trophy>\n"
    "<trophy id=\"002\"><name>Second</name><detail>Do two</detail></trophy>\n</trophyconf>\n";
static const char FR[] =
    "<trophyconf><title-name>Jeu</title-name>\n"
    "<trophy id=\"000\"><name>Platine</name><detail>Tous</detail></trophy>\n"
    "<trophy id=\"001\"><name>Premier</name><detail>Un</detail></trophy>\n"
    "<trophy id=\"002\"><name>Second FR</name><detail>Deux</detail></trophy>\n</trophyconf>\n";
static const u8 PNG0[] = { 0x89, 'P', 'N', 'G', 0, 1, 2, 3 }, ICON[] = { 0x89, 'P', 'N', 'G', 9, 9 };

static void write_trp(const char* path)
{
    struct { const char* name; const void* d; size_t n; } e[] = {
        { "TROPCONF.SFM", CONF, sizeof CONF - 1 }, { "TROP.SFM", EN, sizeof EN - 1 },
        { "TROP_02.SFM", FR, sizeof FR - 1 }, { "ICON0.PNG", ICON, sizeof ICON }, { "TROP000.PNG", PNG0, sizeof PNG0 },
    };
    enum { N = sizeof e / sizeof e[0] };
    static u8 buf[8192];
    memset(buf, 0, sizeof buf);
    put32(buf, 0xDCA24D00u); put32(buf + 4, 1); put32(buf + 0x10, N); put32(buf + 0x14, 0x40);
    size_t off = 0x40 + N * 0x40;
    for (int i = 0; i < N; i++) {
        u8* p = buf + 0x40 + i * 0x40;
        snprintf((char*)p, 32, "%s", e[i].name);
        put64(p + 0x20, off); put64(p + 0x28, e[i].n);
        memcpy(buf + off, e[i].d, e[i].n); off += e[i].n;
    }
    put64(buf + 8, off);
    FILE* f = fopen(path, "wb"); assert(f);
    assert(fwrite(buf, 1, off, f) == off); fclose(f);
}

/* Init, a context on NPWR99999 (registered), as the game does at boot. */
static void boot(void)
{
    assert(sceNpTrophyInit(NULL, 0, 0, 0) == CELL_OK);
    assert(sceNpTrophyCreateContext((void*)0x200, (void*)0x100, NULL, 0) == CELL_OK);
    assert(sceNpTrophyCreateHandle((void*)0x204) == CELL_OK);
    assert(sceNpTrophyRegisterContext(1, 1, 0x300, 0, 0) == CELL_OK);
}

static u32 be32_at(u32 ea) { const u8* p = vm_base + ea; return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static int unlocked_on_disk(const char* json, int id)
{
    FILE* f = fopen(json, "r");
    if (!f) return 0;
    char line[512], want[32]; int got = 0;
    snprintf(want, sizeof want, "\"id\": %d,", id);
    while (fgets(line, sizeof line, f)) if (strstr(line, want)) got = 1;
    fclose(f);
    return got;
}

int main(void)
{
    char root[] = "/tmp/vekta-trophy-XXXXXX";
    assert(mkdtemp(root));
    char trp[256], json[256], tmp[300];
    snprintf(trp, sizeof trp, "%s/TROPHY.TRP", root);
    snprintf(json, sizeof json, "%s/NPWR99999.json", root);
    snprintf(tmp, sizeof tmp, "%s.tmp", json);
    write_trp(trp);
    setenv("PS3_TROPHY_TRP", trp, 1);
    vm_base = calloc(1, 65536); assert(vm_base);
    strcpy((char*)vm_base + 0x400, root);
    sceNpTrophySetStoragePath((void*)0x400);
    memcpy(vm_base + 0x100, "NPWR99999", 10);
    g_ps3_guest_caller = status_cb;
    ps3_trophy_set_notify(on_notice, (void*)0x1234);

    /* English: the notices in unlock order, the platinum after the trophy that completes it */
    unsetenv("PS3_LANG");
    boot();
    assert(ps3_trophy_count() == 3);
    assert(sceNpTrophyUnlockTrophy(1, 1, 0, (void*)0x500) == SCE_NP_TROPHY_ERROR_PLATINUM_CANNOT_UNLOCK);
    assert(sceNpTrophyUnlockTrophy(1, 1, 1, (void*)0x500) == CELL_OK);
    assert(be32_at(0x500) == 0xFFFFFFFFu && heard_n == 1 && heard_id[0] == 1);
    assert(!strcmp(heard_name[0], "First") && !strcmp(heard_comm[0], "NPWR99999"));
    assert(heard_grade[0] == SCE_NP_TROPHY_GRADE_BRONZE);
    /* no repeat */
    assert(sceNpTrophyUnlockTrophy(1, 1, 1, (void*)0x500) == SCE_NP_TROPHY_ERROR_ALREADY_UNLOCKED && heard_n == 1);
    assert(sceNpTrophyUnlockTrophy(1, 1, 2, (void*)0x500) == CELL_OK);
    assert(be32_at(0x500) == 0 && heard_n == 3);
    assert(heard_id[1] == 2 && heard_grade[1] == SCE_NP_TROPHY_GRADE_SILVER);
    assert(heard_id[2] == 0 && heard_grade[2] == SCE_NP_TROPHY_GRADE_PLATINUM && !strcmp(heard_name[2], "Plat"));
    /* the store: written through the temp file, which is gone */
    assert(unlocked_on_disk(json, 0) && unlocked_on_disk(json, 1) && unlocked_on_disk(json, 2));
    assert(access(tmp, F_OK) != 0);
    ps3_trophy_def d;
    assert(ps3_trophy_get(2, &d) == 0 && d.hidden && d.unlocked && d.timestamp && !strcmp(d.name, "Second"));
    assert(ps3_trophy_get(3, &d) == -1 && ps3_trophy_get(-1, &d) == -1);

    /* icons: read from the TRP on demand; -1 is ICON0 */
    size_t n = 0;
    void* png = ps3_trophy_icon(0, &n);
    assert(png && n == sizeof PNG0 && !memcmp(png, PNG0, n)); free(png);
    png = ps3_trophy_icon(-1, &n);
    assert(png && n == sizeof ICON && !memcmp(png, ICON, n)); free(png);
    assert(!ps3_trophy_icon(1, &n) && n == 0);   /* no TROP001.PNG in this set */
    assert(!ps3_trophy_icon(7, &n));
    assert(sceNpTrophyTerm() == CELL_OK);

    /* a reload: the store comes back, the unlocks stay unlocked and are not heard again */
    setenv("PS3_LANG", "fr", 1);
    boot();
    assert(sceNpTrophyGetTrophyUnlockState(1, 1, (void*)0x600, (void*)0x680) == CELL_OK);
    assert(be32_at(0x600) == 0x7u && be32_at(0x680) == 3);
    assert(sceNpTrophyUnlockTrophy(1, 1, 1, (void*)0x500) == SCE_NP_TROPHY_ERROR_ALREADY_UNLOCKED && heard_n == 3);
    /* the language's names for the host, the English ones kept for the store */
    assert(ps3_trophy_get(1, &d) == 0 && !strcmp(d.name, "Premier") && !strcmp(d.name_en, "First"));
    assert(!strcmp(d.detail, "Un"));
    assert(sceNpTrophyTerm() == CELL_OK);
    assert(unlink(json) == 0);

    /* French notices; a language the set lacks falls back to English */
    boot();
    assert(sceNpTrophyUnlockTrophy(1, 1, 2, (void*)0x500) == CELL_OK);
    assert(heard_n == 4 && !strcmp(heard_name[3], "Second FR"));
    assert(sceNpTrophyTerm() == CELL_OK);
    assert(unlink(json) == 0);
    setenv("PS3_LANG", "ja", 1);
    boot();
    assert(ps3_trophy_get(1, &d) == 0 && !strcmp(d.name, "First"));
    assert(sceNpTrophyTerm() == CELL_OK);
    unlink(json);

    /* blocked: the title is told it worked, nothing is heard or stored */
    unsetenv("PS3_LANG");
    boot();
    ps3_trophy_set_blocked(1);
    assert(sceNpTrophyUnlockTrophy(1, 1, 1, (void*)0x500) == CELL_OK && be32_at(0x500) == 0xFFFFFFFFu);
    assert(heard_n == 4 && ps3_trophy_get(1, &d) == 0 && !d.unlocked);
    ps3_trophy_set_blocked(0);
    assert(sceNpTrophyUnlockTrophy(1, 1, 1, (void*)0x500) == CELL_OK && heard_n == 5);
    assert(sceNpTrophyTerm() == CELL_OK);
    assert(unlocked_on_disk(json, 1) && !unlocked_on_disk(json, 2));

    /* the temp folder: the store, its log and the TRP, nothing else */
    char path[512];
    snprintf(path, sizeof path, "%s/NPWR99999.log", root);
    unlink(path); unlink(json); unlink(trp);
    DIR* dir = opendir(root); assert(dir);
    for (struct dirent* e; (e = readdir(dir));)
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { fprintf(stderr, "left: %s\n", e->d_name); assert(0); }
    closedir(dir);
    assert(rmdir(root) == 0);
    free(vm_base);
    puts("trophy notices, store, language, block and icons: ok");
    return 0;
}
