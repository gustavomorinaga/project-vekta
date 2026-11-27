// port/kz1hd/rl_bridge.cpp against the Recomp Achievements SDK, with the HLE's host calls faked here: a set of four
// trophies (0 the platinum, 1 earned before the session), one save load, an unlock of 2 and of the platinum.
// Run by test_rl_bridge.sh: "channel" under tools/tests/rl_launcher.py (RL_SESSION set), "none" without it.
#include "rl_bridge.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "cellSaveData.h"
#include "sceNpTrophy.h"

static int s_frame;
static unsigned s_loads;

// the set appears on frame 2 (CreateContext) and the store on frame 3 (RegisterContext): the bridge waits for the
// store, or 1 would read locked and not go out as earlier
extern "C" int ps3_trophy_count(void) { return s_frame >= 2 ? 4 : 0; }
extern "C" int ps3_trophy_ready(void) { return s_frame >= 3; }
extern "C" int ps3_trophy_get(s32 id, ps3_trophy_def* out)
{
    static const char* const names[] = { "Platinum", "First", "Second", "Third" };
    if (id < 0 || id >= ps3_trophy_count()) return -1;
    std::memset(out, 0, sizeof *out);
    out->id = id;
    out->grade = id == 0 ? SCE_NP_TROPHY_GRADE_PLATINUM : SCE_NP_TROPHY_GRADE_BRONZE;
    out->unlocked = id == 1 && ps3_trophy_ready();
    std::snprintf(out->name, sizeof out->name, "%s", names[id]);
    std::snprintf(out->detail, sizeof out->detail, "Do the %s thing", names[id]);
    return 0;
}
extern "C" unsigned ps3_savedata_loads(void) { return s_loads; }

static void frame(const int* ids, unsigned n)
{
    kz_rl_frame(ids, n);
    s_frame++;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

int main(int argc, char** argv)
{
    const bool channel = argc > 1 && !std::strcmp(argv[1], "channel");
    kz_rl_set_casual("debug");
    for (int i = 0; i < 5; i++) frame(nullptr, 0);   // waits, then connects on frame 3
    s_loads = 1;                                     // the profile loads
    frame(nullptr, 0);
    const int unlocked[] = { 2, 0 };                 // 2, and the platinum (never sent)
    frame(unlocked, 2);
    for (int i = 0; i < 200 && !kz_rl_quiet(); i++) frame(nullptr, 0);   // the launcher's quiet
    std::printf("quiet=%d\n", kz_rl_quiet());
    if (!channel && kz_rl_quiet()) { std::fprintf(stderr, "FAIL: quiet without a launcher\n"); return 1; }
    return 0;
}
