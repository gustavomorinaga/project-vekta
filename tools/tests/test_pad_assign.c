/* Which controllers feed which port (libs/input/pad_assign.h, patch 4020): one player as before, two players with
 * the keyboard and mouse as player 1, port 1 connected whenever a pad is there. */
#include "pad_assign.h"
#include <assert.h>
#include <stdio.h>

static PadAssign run(int s0, int s1, int s2, int s3, int players, int merged)
{
    const int has[PA_PORTS] = { s0, s1, s2, s3 };
    PadAssign a;
    pad_assign(has, players, merged, &a);
    /* switching between 1 and 2 players never plugs or unplugs a port */
    PadAssign b;
    pad_assign(has, players == 2 ? 1 : 2, merged, &b);
    for (int p = 0; p < PA_PORTS; p++) assert(a.connected[p] == b.connected[p]);
    assert(a.connected[0]);
    return a;
}
#define SRC(a, x0, x1, x2, x3) assert((a).src[0] == (x0) && (a).src[1] == (x1) && (a).src[2] == (x2) && (a).src[3] == (x3))
#define CON(a, c1, c2, c3) assert((a).connected[1] == (c1) && (a).connected[2] == (c2) && (a).connected[3] == (c3))

int main(void)
{
    PadAssign a;
    /* no pad: the keyboard is port 0, port 1 is not there (Two Player stays greyed out) */
    for (int m = 0; m < 2; m++) for (int pl = 1; pl <= 2; pl++) { a = run(0, 0, 0, 0, pl, m); SRC(a, 0, 0, 0, 0); CON(a, 0, 0, 0); }

    /* 1 player, merged: every pad on port 0, as before; port 1 connected and fed by nothing (neutral) */
    a = run(1, 0, 0, 0, 1, 1); SRC(a, 1, 0, 0, 0); CON(a, 1, 0, 0);
    a = run(1, 1, 1, 0, 1, 1); SRC(a, 7, 0, 0, 0); CON(a, 1, 0, 0);
    a = run(0, 1, 0, 0, 1, 1); SRC(a, 2, 0, 0, 0); CON(a, 1, 0, 0);

    /* 1 player, separate: slot n on port n, as before */
    a = run(1, 0, 0, 0, 1, 0); SRC(a, 1, 0, 0, 0); CON(a, 1, 0, 0);
    a = run(1, 1, 1, 0, 1, 0); SRC(a, 1, 2, 4, 0); CON(a, 1, 1, 0);
    a = run(0, 1, 0, 0, 1, 0); SRC(a, 0, 2, 0, 0); CON(a, 1, 0, 0);

    /* 2 players, 1 pad: keyboard and mouse = player 1 (port 0), the pad = player 2 (port 1), whichever slot */
    for (int m = 0; m < 2; m++) {
        a = run(1, 0, 0, 0, 2, m); SRC(a, 0, 1, 0, 0); CON(a, 1, 0, 0);
        a = run(0, 0, 1, 0, 2, m); SRC(a, 0, 4, 0, 0); CON(a, 1, !m, 0);   /* separate: its own port stays connected */
    }
    /* 2 players, 2+ pads: the first pad with the keyboard is player 1, the second player 2, the rest none */
    a = run(1, 1, 0, 0, 2, 1); SRC(a, 1, 2, 0, 0); CON(a, 1, 0, 0);
    a = run(0, 1, 0, 1, 2, 1); SRC(a, 2, 8, 0, 0); CON(a, 1, 0, 0);
    a = run(1, 1, 1, 1, 2, 1); SRC(a, 1, 2, 0, 0); CON(a, 1, 0, 0);
    a = run(1, 1, 1, 1, 2, 0); SRC(a, 1, 2, 0, 0); CON(a, 1, 1, 1);   /* separate: ports 2-3 stay connected, neutral */
    puts("PASS pad_assign");
    return 0;
}
