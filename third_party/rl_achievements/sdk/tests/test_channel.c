/* Run by fake_launcher.py with RL_SESSION set: talks to the "launcher" over the channel. */
#include "rl_achievements.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uint8_t ram[0x100];
static uint32_t read_ram(uint32_t a, uint8_t* b, uint32_t n, void* ud) {
  (void)ud;
  if (a >= sizeof ram) return 0;
  if (a + n > sizeof ram) n = (uint32_t)(sizeof ram - a);
  memcpy(b, ram + a, n);
  return n;
}

int main(void) {
  int i;
  rl_ach_set_info("supermarioworldrecomp", "1.4.0");
  rl_ach_native_define(42, "Pants On", "Put on your pants", 20);
  if (!rl_ach_init("snes", read_ram, NULL)) { printf("not connected\n"); return 1; }
  for (i = 0; i < 200; i++) { rl_ach_frame(); usleep(2000); } /* the launcher sends the list */
  ram[0x19] = 2; for (i = 0; i < 3; i++) rl_ach_frame();     /* rule fires */
  rl_ach_save_loaded();
  rl_ach_native(42);
  rl_ach_event("beat_bowser");
  rl_ach_lboard(9, 1234);                                     /* a port's own leaderboard entry */
  ram[0x21] = 77; ram[0x20] = 1; for (i = 0; i < 3; i++) rl_ach_frame();   /* leaderboard 5 starts */
  ram[0x20] = 3; for (i = 0; i < 3; i++) rl_ach_frame();                  /* ...and submits 77 */
  ram[0x22] = 3; for (i = 0; i < 320; i++) rl_ach_frame();                /* rich presence: World 3 */
  for (i = 0; i < 100; i++) { rl_ach_frame(); usleep(2000); } /* answer a peek */
  printf("quiet=%d\n", rl_ach_quiet_toasts());
  rl_ach_shutdown();
  return 0;
}
