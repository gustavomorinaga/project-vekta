// Trophies on the port's side (docs/dev/trophies.md): the unlocks the HLE reports, queued for the frame loop.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Once at boot, before the game runs: the HLE's notify callback, and the debug-options block.
void kz_trophies_register(void);
// Once per frame on the main loop: takes the queued unlocks (stage 2 draws them; for now they are logged).
void kz_trophies_pump(void);

#ifdef __cplusplus
}
#endif
