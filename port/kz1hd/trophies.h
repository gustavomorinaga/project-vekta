// Trophies on the port's side (docs/dev/trophies.md): the unlocks the HLE reports, queued for the frame loop.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Once at boot, before the game runs: the HLE's notify callback, and the debug-options block.
void kz_trophies_register(void);
// Once per frame on the main loop: takes the queued unlocks (logged, and shown by trophy_notice.cpp).
void kz_trophies_pump(void);

// The notice on screen and its sound (trophy_notice.cpp): once at boot (the overlay and mix hooks), the PC page's
// two rows (trophy_notices, trophy_sound in kz1hd_pc.ini; at start and when they change), and one unlock to show.
void kz_notice_init(void);
void kz_notice_options(int notices, int sound);
void kz_notice_add(int id, int grade, const char* name, int got, int total);
// The Trophies page (pcoptions.cpp) is on screen: called every frame it is, it shows the page's header (the count
// and its bar) in the notices' place, the notices under it.
void kz_trophy_page_ping(void);

// The Trophies page: moves when the frame loop takes an unlock report or the set's size changes (its data is made
// again then).
unsigned kz_trophy_gen(void);

#ifdef __cplusplus
}

#include <string>
#include <vector>
// The page's data as Lua (trophy_list.h), in pieces of at most max_bytes, from the HLE's store now.
std::vector<std::string> kz_trophy_page_lua(size_t max_bytes);
#endif
