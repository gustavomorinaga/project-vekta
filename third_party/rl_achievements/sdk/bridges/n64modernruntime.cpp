// Bridge for N64ModernRuntime (N64Recomp). Draft for a pull request.
// Memory lists use RDRAM offsets (0x000000 to 0x7FFFFF), the same addresses N64
// emulators and decomp symbol maps use. N64Recomp keeps RDRAM word-swapped:
// the byte at address A sits at rdram[A ^ 3].

#include <rl_achievements.h>

static uint8_t* g_rdram = nullptr;
static constexpr uint32_t kRdramSize = 8 * 1024 * 1024;

static uint32_t read_rdram(uint32_t address, uint8_t* buffer, uint32_t num_bytes, void*) {
  address &= 0x1FFFFFFF;                       // accept KSEG0/KSEG1 style addresses too
  if (!g_rdram || address >= kRdramSize) return 0;
  if (address + num_bytes > kRdramSize) num_bytes = kRdramSize - address;
  for (uint32_t i = 0; i < num_bytes; i++) buffer[i] = g_rdram[(address + i) ^ 3];
  return num_bytes;
}

// librecomp: in the game's on_init_callback (it receives rdram):
//   g_rdram = rdram;
//   rl_ach_set_info(game_id, version);
//   rl_ach_init("n64", read_rdram, nullptr);
//
// ultramodern events: in vi_callback (once per VI):
//   rl_ach_frame();
