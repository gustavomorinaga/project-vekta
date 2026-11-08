/* The Lua states given the PC script (port/kz1hd/lua_states.h): over a session with many more states than the list
 * holds, each new state is given the script once and then taken as given at every later binding call, so the
 * script is compiled once per state and not at every call. Before, the list stopped at 16: from the 17th state on
 * every call compiled the whole script again (68 to 90 fps from the 6th level of a scripted run). */
#include <stdio.h>
#include "lua_states.h"

int main(void)
{
    struct lua_states s = { { 0 }, 0, 0 };
    unsigned compiles = 0;
    /* 40 states one after another (a long session), each making 1000 binding calls while it is the live one */
    for (unsigned st = 0; st < 40; st++) {
        const uint32_t L = 0x11000000u + st * 0x10000u;
        unsigned mine = 0;
        for (int call = 0; call < 1000; call++)
            if (!lua_states_seen(&s, L)) { lua_states_add(&s, L); compiles++; mine++; }
        if (mine != 1) { fprintf(stderr, "FAIL: state %u compiled %u times\n", st + 1, mine); return 1; }
    }
    printf("40 states, 1000 calls each: %u compiles\n", compiles);
    if (compiles != 40) { fprintf(stderr, "FAIL: %u compiles for 40 states\n", compiles); return 1; }
    /* two live states taking turns (the level's and a menu's), after the list has wrapped */
    const uint32_t A = 0x12340000u, B = 0x12350000u;
    compiles = 0;
    for (int call = 0; call < 2000; call++) {
        const uint32_t L = (call & 1) ? A : B;
        if (!lua_states_seen(&s, L)) { lua_states_add(&s, L); compiles++; }
    }
    if (compiles != 2) { fprintf(stderr, "FAIL: two alternating states compiled %u times\n", compiles); return 1; }
    puts("two alternating states: one compile each");
    /* a state at a freed one's address is taken as given: the every-50th-call probe ("not KZPCROWS") gives it
     * the script, which lua_states cannot see; here only that it is not compiled again and again */
    compiles = 0;
    for (int call = 0; call < 1000; call++)
        if (!lua_states_seen(&s, A)) { lua_states_add(&s, A); compiles++; }
    if (compiles != 0) { fprintf(stderr, "FAIL: a reused address compiled %u times\n", compiles); return 1; }
    puts("a reused address: no compile here (the probe covers it)");
    puts("test_lua_states: ok");
    return 0;
}
