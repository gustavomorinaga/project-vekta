/*
 * A sound's random pitch, on a generator of its own while a route records or
 * replays (../route.cpp). See native_ppu.h.
 *
 * func_00082B58(voice r3, sample r4) starts a sample with a random pitch drawn
 * from the game's generator (0x012D8018). It is reached from func_001A6144
 * only once the sound system (0x007E3218) has the sample resident, and the
 * samples stream in on other threads: how often the shared generator is
 * advanced, and so every number the game draws after it, depended on load
 * timing, and a replay drifted from its recording. Outside a route this is
 * the lifted body as it is.
 */
#include "native_ppu.h"
#include "../route.h"

void func_00082B58(ppu_context* ctx)
{
    kz_route_sound_rng(1);
    func_00082B58_lifted(ctx);
    kz_route_sound_rng(0);
}
