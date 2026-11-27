/* Memory rules with no launcher: the waiting rule, a normal unlock, progress, a broken rule. */
#include "rl_achievements.h"
#include <stdio.h>
#include <string.h>

static uint8_t ram[0x100];
static int unlocked[8];

static uint32_t read_ram(uint32_t a, uint8_t* b, uint32_t n, void* ud) {
  (void)ud;
  if (a >= sizeof ram) return 0;
  if (a + n > sizeof ram) n = (uint32_t)(sizeof ram - a);
  memcpy(b, ram + a, n);
  return n;
}
static void got(uint32_t id, void* ud) { (void)ud; if (id < 8) unlocked[id]++; }
static void frames(int n) { while (n--) rl_ach_frame(); }
static int fails;
#define CHECK(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)

int main(void) {
  int n;
  CHECK(rl_ach_init("snes", read_ram, NULL) == 0, "no RL_SESSION: not connected, no error");
  rl_ach_native(5); rl_ach_event("x"); rl_ach_save_loaded(); /* must be harmless */
  rl_ach_on_unlock(got, NULL);

  ram[0x10] = 7; /* a loaded save that already meets rule 1 */
  n = rl_ach_load_list("{\"achievements\":["
                       "{\"id\":1,\"trigger\":\"0xH0010=7\"},"
                       "{\"id\":2,\"trigger\":\"0xH0011>=3_0xH0012=1\"},"
                       "{\"id\":3,\"trigger\":\"M:0xH0020>=5\"},"
                       "{\"id\":4,\"trigger\":\"not a rule\"},"
                       "{\"id\":6,\"native\":12}]}");
  CHECK(n == 3, "3 rules loaded, the bad one and the native one skipped");

  frames(120);
  CHECK(unlocked[1] == 0, "rule already true at load does not fire (waiting state)");
  ram[0x10] = 6; frames(1); ram[0x10] = 7; frames(1);
  CHECK(unlocked[1] == 1, "fires once the value really reaches 7 during play");
  frames(60);
  CHECK(unlocked[1] == 1, "fires only once");

  ram[0x11] = 3; frames(5);
  CHECK(unlocked[2] == 0, "two-condition rule waits for both");
  ram[0x12] = 1; frames(1);
  CHECK(unlocked[2] == 1, "two-condition rule fires");

  ram[0x20] = 2; frames(2); ram[0x20] = 5; frames(1);
  CHECK(unlocked[3] == 1, "measured rule fires at its target");

  rl_ach_shutdown();
  printf(fails ? "\n%d failed\n" : "\nall passed\n", fails);
  return fails != 0;
}
