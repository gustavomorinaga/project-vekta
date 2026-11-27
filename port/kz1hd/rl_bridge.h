// RecompLauncher's achievements (Recomp Achievements SDK, third_party/rl_achievements; docs/dev/trophies.md, stage 4):
// the game's trophies reported to the launcher over its local pipe. Started without RecompLauncher (no RL_SESSION),
// built with KZ_RL_ACHIEVEMENTS=OFF, or with KZ_RL_ACH=0 in the environment, every call does nothing.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Once per frame on the frame loop (kz_trophies_pump), with the trophy ids unlocked since the last frame. The first
// frame with RL_SESSION set waits for the trophy set and the player's store (ps3_trophy_ready: the game registered its
// context; at most 60 s), then sends the names (native_list), connects (hello), and reports the trophies already earned
// (earlier), the save loads (save_loaded) and the session's casual reason; then each new unlock (native). The
// platinum (id 0) is never sent: the hosted list leaves it out.
void kz_rl_frame(const int* ids, unsigned n);

// A debug option or a scripted input is on (trophies.cpp, blocking_option): the session is Casual for the launcher.
// Before the first frame; the reason is a short word the launcher shows.
void kz_rl_set_casual(const char* reason);

// 1 once the launcher said it shows its own toast ("quiet"): the port's notice and its sound are not shown.
int kz_rl_quiet(void);

#ifdef __cplusplus
}
#endif
