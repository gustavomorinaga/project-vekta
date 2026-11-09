/* KZ_DEBUG_GOD (port/kz1hd/debug_god.h): which values turn it on, and the flag's address from the settings pointer
 * (none before the settings exist). */
#include <stdio.h>
#include "debug_god.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)

int main(void)
{
    EXPECT(debug_god_on("1"));
    EXPECT(debug_god_on("yes"));
    EXPECT(!debug_god_on("0"));
    EXPECT(!debug_god_on(""));
    EXPECT(!debug_god_on(NULL));
    EXPECT(debug_god_flag(0) == 0);
    EXPECT(debug_god_flag(0x10002000u) == 0x10002131u);
    EXPECT(DEBUG_GOD_SETTINGS == 0x00828734u);
    if (fails) return 1;
    puts("test_debug_god: ok");
    return 0;
}
