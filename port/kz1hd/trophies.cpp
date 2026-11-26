// Trophies on the port's side (docs/dev/trophies.md). The HLE (sceNpTrophy.c, patch 5002) calls on_notice on the
// game's unlock thread (func_001D2E48) for each new unlock; the notice and the count so far are copied into a fixed
// ring under a mutex, with no allocation on that thread, and kz_trophies_pump takes it on the frame loop and hands
// it to the notice (trophy_notice.cpp).
#include "trophies.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "sceNpTrophy.h"

namespace {

struct Notice {
    int id, grade, have, total;   // have: unlocked with this one (the store's count when it was told)
    bool hidden;
    char name[SCE_NP_TROPHY_NAME_MAX_SIZE];
};

// shortcut: 16 notices; more unlocks than that between two frames drop the oldest (the store keeps them all)
constexpr unsigned kRing = 16;
Notice s_ring[kRing];
unsigned s_head, s_count;   // s_head: the oldest
std::mutex s_lock;

void on_notice(const ps3_trophy_notice* n, void*)
{
    // counted here, on the unlocking thread: the store has this unlock and not yet the next one
    // (the HLE calls this with its lock released)
    const int total = ps3_trophy_count();
    int have = 0;
    ps3_trophy_def def;
    for (int id = 0; id < total; id++) have += ps3_trophy_get(id, &def) == 0 && def.unlocked;
    std::lock_guard<std::mutex> g(s_lock);
    if (s_count == kRing) { s_head = (s_head + 1) % kRing; s_count--; }
    Notice& d = s_ring[(s_head + s_count++) % kRing];
    d.id = n->id;
    d.grade = n->grade;
    d.have = have;
    d.total = total;
    d.hidden = n->hidden != 0;
    std::snprintf(d.name, sizeof d.name, "%s", n->name ? n->name : "");
}

const char* grade_name(int g)
{
    return g == SCE_NP_TROPHY_GRADE_PLATINUM ? "platinum" : g == SCE_NP_TROPHY_GRADE_GOLD ? "gold"
         : g == SCE_NP_TROPHY_GRADE_SILVER ? "silver" : g == SCE_NP_TROPHY_GRADE_BRONZE ? "bronze" : "?";
}

// The debug switches that change what the player can do, and the scripted inputs (a run driven by a control script
// is not the player's): with any of them on, no trophy is stored (docs/dev/trophies.md, "Debug options that block
// trophies").
const char* blocking_option()
{
    static const char* const names[] = { "KZ_UNLOCK_LEVELS", "KZ_DEBUG_MENU", "KZ_DEBUG_GOD", "KZ_DEBUG_GIVE",
                                         "KZ_POKE", "PAD_SCRIPT", "PAD_FILE", "PAD_STICK", "PAD_STICK_FILE",
                                         "PAD_SWEEP", "PAD_AUTOPRESS", "PAD_VIRTUAL_FILE", "PAD_MOUSE_FAKE",
                                         "PAD_MOUSE_POS", "PAD_MOUSE_POS_FILE", "KZ_ROUTE_PLAY" };
    for (const char* n : names)
        if (const char* v = std::getenv(n); v && *v) return n;
    return nullptr;
}

}  // namespace

extern "C" void kz_trophies_register(void)
{
    ps3_trophy_set_notify(on_notice, nullptr);
    kz_notice_init();
    if (const char* why = blocking_option()) {
        ps3_trophy_set_blocked(1);
        std::fprintf(stderr, "[trophy] %s is set: trophies are not stored this session\n", why);
    }
}

extern "C" void kz_trophies_pump(void)
{
    Notice got[kRing];
    unsigned n = 0;
    {
        std::lock_guard<std::mutex> g(s_lock);
        for (; s_count; s_count--, s_head = (s_head + 1) % kRing) got[n++] = s_ring[s_head];
    }
    for (unsigned i = 0; i < n; i++) {
        std::fprintf(stderr, "[trophy] unlocked %d %s (%s)\n", got[i].id, got[i].name, grade_name(got[i].grade));
        kz_notice_add(got[i].id, got[i].grade, got[i].name, got[i].have, got[i].total);
    }
}
