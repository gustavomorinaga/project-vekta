// RecompLauncher's achievements (rl_bridge.h; docs/dev/trophies.md, stage 4). ENHANCEMENT: the PS3 had no such
// launcher. The game's trophies are "native" achievements for Recomp Achievements (third_party/rl_achievements,
// docs/SPEC.md): the launcher hosts the list (titles, points; no platinum), the port reports trophy ids. Everything
// runs on the frame loop, the SDK's calls included. The port never talks to a server: the SDK writes one JSON line
// per message to the local pipe or socket named by RL_SESSION, and only when it is set.
// The shape (events drained once per frame, names from the player's TRP, quiet hiding the port's own notice) follows
// SamiEnazi/infamous2-port's engine/libs/misc/ps3_recomp_achievements.c (MIT); the code is written here.
#include "rl_bridge.h"

#if KZ_RL_ACH
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cellSaveData.h"    // ps3_savedata_loads (patch 3040)
#include "kz_version.h"      // VEKTA_VER_STR
#include "rl_achievements.h"
#include "sceNpTrophy.h"     // ps3_trophy_count, ps3_trophy_get (patch 5002), ps3_trophy_ready (patch 5003)
#endif

namespace {

int s_state;              // 0 the first frame not seen, 1 waiting for the set, 2 connected, -1 off
unsigned s_loads;         // save loads already reported
const char* s_casual;     // kz_rl_set_casual's reason, sent once connected
int s_quiet;

#if KZ_RL_ACH
// ASSUMPTION: 60 s is enough for the boot to register the context (about 5 s on the maintainer's PC); a title without
// a TRP never does, and connects then with no list. Time, not frames: the loop turns thousands of times during boot.
constexpr auto kWait = std::chrono::seconds(60);
std::chrono::steady_clock::time_point s_since;

void start()
{
    const int total = ps3_trophy_count();
    ps3_trophy_def d;
    // the names before rl_ach_init: they go out as one native_list right after hello (after it, each define would
    // send the whole list again). PS3 trophies have no gamerscore: the hosted list gives the points.
    for (int id = 1; id < total; id++)
        if (ps3_trophy_get(id, &d) == 0) rl_ach_native_define((uint32_t)id, d.name, d.detail, 0);
    rl_ach_set_info("", VEKTA_VER_STR);
    if (!rl_ach_init("ps3", nullptr, nullptr)) {
        s_state = -1;
        std::fprintf(stderr, "[rl] RL_SESSION is set but RecompLauncher's channel did not open: trophies are not "
                             "reported\n");
        return;
    }
    s_state = 2;
    int earlier = 0;
    for (int id = 1; id < total; id++)
        if (ps3_trophy_get(id, &d) == 0 && d.unlocked) { rl_ach_native_earlier((uint32_t)id); earlier++; }
    if (s_casual) rl_ach_casual(s_casual);
    std::fprintf(stderr, "[rl] connected to RecompLauncher: %d trophies listed, %d earlier%s%s\n",
                 total > 0 ? total - 1 : 0, earlier, s_casual ? ", casual: " : "", s_casual ? s_casual : "");
}
#endif

}  // namespace

extern "C" void kz_rl_set_casual(const char* reason) { s_casual = reason; }

extern "C" int kz_rl_quiet(void) { return s_quiet; }

extern "C" void kz_rl_frame(const int* ids, unsigned n)
{
    if (s_state < 0) return;
#if KZ_RL_ACH
    if (s_state == 0) {
        const char* session = std::getenv("RL_SESSION");
        const char* sw = std::getenv("KZ_RL_ACH");
        s_state = !session || !*session ? -1 : sw && !std::strcmp(sw, "0") ? -1 : 1;
        if (s_state < 0) {
            if (session && *session) std::fprintf(stderr, "[rl] KZ_RL_ACH=0: trophies are not reported\n");
            return;
        }
        s_since = std::chrono::steady_clock::now();
    }
    if (s_state == 1) {
        // the set and the player's store (RegisterContext): before it every trophy reads locked
        if (!ps3_trophy_ready() && std::chrono::steady_clock::now() - s_since < kWait) return;
        start();   // the unlocks of the frames waited are in the store: sent as earlier
        if (s_state < 0) return;
    } else {
        for (unsigned i = 0; i < n; i++)
            if (ids[i] > 0) rl_ach_native((uint32_t)ids[i]);
    }
    if (const unsigned loads = ps3_savedata_loads(); loads != s_loads) {   // one message for the loads of a frame
        s_loads = loads;
        rl_ach_save_loaded();
    }
    rl_ach_frame();
    if (!s_quiet && rl_ach_quiet_toasts()) {
        s_quiet = 1;
        std::fprintf(stderr, "[rl] RecompLauncher shows the unlock toasts: the port's notice is hidden\n");
    }
#else
    (void)ids; (void)n; (void)s_loads;
    s_state = -1;
#endif
}
