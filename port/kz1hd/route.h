/* Routes: a play recorded tick by tick and replayed (route.cpp). */
#ifndef KZ_ROUTE_H
#define KZ_ROUTE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* native_ppu/clock.cpp, at the start of a frame. Returns 1 when a replayed
 * frame is due, with the ticks it advances and the clock's tick counts. */
int  kz_route_clock(uint32_t clock, int32_t* n, int32_t* now, int32_t* prev);
/* After the game's own frame clock ran (recording). */
void kz_route_clock_done(uint32_t clock);
/* pcoptions.cpp, the player controller's tick (func_00291FA8). */
void kz_route_tick_begin(void);
void kz_route_mouse(int* dx, int* dy);
void kz_route_tick_end(uint32_t controller);
/* native_ppu/sound_rng.cpp and the hook of func_001A5380: 1 as a sound
 * starts drawing random numbers, 0 when it is done. While a route records or
 * replays they come from a generator of the sound's own. */
void kz_route_sound_rng(int enter);
/* stubs.cpp: the hooks, when KZ_ROUTE_REC or KZ_ROUTE_PLAY is set. */
void kz_route_register(void);
#ifdef __cplusplus
}
#endif
#endif
