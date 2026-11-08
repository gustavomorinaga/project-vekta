/*
 * The Lua states the PC script was given (pcoptions.cpp, lua_binding).
 * Header-only, plain C, so a host test can check it
 * (tools/tests/test_lua_states.c).
 *
 * The list held 16 states and stopped there: from the 17th state of a session
 * on (each level and screen makes two to five), a new state was never marked
 * as given, so the whole script was compiled again at every binding call in
 * it, not once: about 70 % of the game's thread in a profile of Level 4, and
 * 68 to 90 fps from the 6th level of a scripted run (2026-11-08). It is now a
 * ring: the oldest entry makes room. A state pushed out while still alive
 * gets the script once more; a new state at a freed one's address is taken
 * as given, and the probe every 50th call ("not KZPCROWS", set by the script
 * in each state it runs in) gives it the script then.
 */
#ifndef KZ_LUA_STATES_H
#define KZ_LUA_STATES_H
#include <stdint.h>

#define LUA_STATES_MAX 16

struct lua_states {
    uint32_t L[LUA_STATES_MAX];
    unsigned n;      /* entries in use, up to LUA_STATES_MAX */
    unsigned next;   /* the entry the next new state takes */
};

static inline int lua_states_seen(const struct lua_states* s, uint32_t L)
{
    for (unsigned i = 0; i < s->n; i++) if (s->L[i] == L) return 1;
    return 0;
}

/* Records L (not already there), over the oldest entry once full. */
static inline void lua_states_add(struct lua_states* s, uint32_t L)
{
    s->L[s->next] = L;
    s->next = (s->next + 1) % LUA_STATES_MAX;
    if (s->n < LUA_STATES_MAX) s->n++;
}
#endif
