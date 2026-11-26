/* Patch 4018: a press a title hook makes (pad_inject, the PC menus' mouse clicks) ends PAD_INJECT_MS after it was
 * made, read or not. Counted in reads alone, a click on Killzone HD's Extras > War Scenes was still held when the
 * bot match's attract mode looked for "any button" after the load, and the match ended at once. */
#include <stdio.h>
#include "pad_inject.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)

int main(void)
{
    /* two reads in a menu, a frame apart: both carry it, then it is over */
    int reads = 2; unsigned long long t0 = 1000, until = t0 + PAD_INJECT_MS;
    EXPECT(pad_inject_due(&reads, until, t0 + 1) == 1);
    EXPECT(pad_inject_due(&reads, until, t0 + 17) == 1);
    EXPECT(pad_inject_due(&reads, until, t0 + 33) == 0);
    EXPECT(reads == 0);

    /* one read, then the title loads for 2.4 s: the second read comes after the press ended (the bug carried it) */
    reads = 2;
    EXPECT(pad_inject_due(&reads, until, t0 + 1) == 1);
    EXPECT(pad_inject_due(&reads, until, t0 + 2400) == 0);
    EXPECT(reads == 0);
    EXPECT(pad_inject_due(&reads, until, t0 + 2417) == 0);

    /* exactly at the end still counts; nothing asked, nothing pressed */
    reads = 1; EXPECT(pad_inject_due(&reads, until, until) == 1);
    reads = 0; EXPECT(pad_inject_due(&reads, until, t0) == 0);
    EXPECT(PAD_INJECT_MS >= 66);   /* two reads at 30 fps fit */

    if (fails) return 1;
    puts("test_pad_inject: ok");
    return 0;
}
