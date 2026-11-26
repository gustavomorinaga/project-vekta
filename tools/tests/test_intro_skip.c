/* intro = off blanks only the start-up open of Movies/Intro/Intro.avi (port/kz1hd/intro_skip.h). Before the fix every
 * open was blank, so Extras > Game Intro showed one black frame and went back to the menu. */
#include <stdio.h>
#include "intro_skip.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)

int main(void)
{
    /* intro = off: the start-up open is blank, the menu's own movies and Extras > Game Intro are not */
    int opened = 0;
    EXPECT(kz_intro_blank(1, 0, &opened) == 0);   /* a logo or menu movie before the intro */
    EXPECT(opened == 0);
    EXPECT(kz_intro_blank(1, 1, &opened) == 1);   /* start-up */
    EXPECT(kz_intro_blank(1, 0, &opened) == 0);   /* the main menu's background */
    EXPECT(kz_intro_blank(1, 1, &opened) == 0);   /* Extras > Game Intro */
    EXPECT(kz_intro_blank(1, 1, &opened) == 0);   /* and again */

    /* intro = on: never blank */
    opened = 0;
    EXPECT(kz_intro_blank(0, 1, &opened) == 0);
    EXPECT(kz_intro_blank(0, 1, &opened) == 0);
    EXPECT(kz_intro_blank(0, 0, &opened) == 0);

    if (fails) return 1;
    puts("test_intro_skip: ok");
    return 0;
}
