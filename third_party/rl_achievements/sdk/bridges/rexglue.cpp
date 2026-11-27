// Bridge for ReXGlue SDK (Xbox 360). Draft for a pull request to rexglue/rexglue-sdk.
// ReXGlue already catches XGIUserWriteAchievements and keeps its own unlock list
// (AchievementManager, since June 2026). This forwards each new unlock to RecompLauncher
// and lets the launcher's toast replace ReXGlue's own when it's connected.
//
// In KernelState, after the achievement metadata and unlock save path are set up
// (src/system/kernel_state.cpp, next to LoadMetadataFile / SetUnlockSavePath):

#include <rl_achievements.h>

  // The names, descriptions and gamerscore ReXGlue read from the XEX: the launcher can't decrypt it.
  for (const auto& a : achievement_manager_.ListAchievements())
    rl_ach_native_define(a.id, a.label.c_str(), a.description.c_str(), a.gamerscore);
  if (rl_ach_init("x360", nullptr, nullptr)) {
    // Unlocks this player already had before the bridge existed: sent once, the
    // launcher records them as earlier (Casual) so they can't jump the leaderboards.
    for (const auto& a : achievement_manager_.ListAchievements())
      if (achievement_manager_.IsUnlocked(a.id)) rl_ach_native_earlier(a.id);
    achievement_manager_.RegisterUnlockCallback(
        [](const AchievementEvent& e) { rl_ach_native(e.achievement.id); });
  }

// KernelState::UnlockAchievement: keep ReXGlue's toast unless the launcher shows its own.
void KernelState::UnlockAchievement(uint32_t id) {
  (void)achievement_manager_.UnlockAchievement(
      id, rl_ach_quiet_toasts() ? AchievementNotification::kSuppress : AchievementNotification::kShow);
}

// Once per frame (the presenter's frame hook): reads launcher messages such as "quiet".
//   rl_ach_frame();
//
// When the game opens its save (xeXamContentCreate on a savegame content type):
//   rl_ach_save_loaded();
