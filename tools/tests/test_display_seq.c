/* KZ_TEST_DISPLAY_SEQ's parsing (port/kz1hd/display_seq.h): the steps the in-game display test makes. */
#include <stdio.h>
#include <string.h>
#include "display_seq.h"

static int fails;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); fails++; } } while (0)

int main(void)
{
    struct display_step d[DISPLAY_SEQ_MAX];
    EXPECT(display_seq_parse("", d, DISPLAY_SEQ_MAX) == 0);
    EXPECT(display_seq_parse(NULL, d, DISPLAY_SEQ_MAX) == 0);
    int n = display_seq_parse("130:window:3440x1440:-,150.5:-:-:200,170:fullscreen:1280x720:50", d, DISPLAY_SEQ_MAX);
    EXPECT(n == 3);
    EXPECT(d[0].at == 130.0 && !strcmp(d[0].mode, "window") && d[0].w == 3440 && d[0].h == 1440 && d[0].scale == 0);
    EXPECT(d[1].at == 150.5 && d[1].mode[0] == 0 && d[1].w == 0 && d[1].scale == 200);
    EXPECT(!strcmp(d[2].mode, "fullscreen") && d[2].w == 1280 && d[2].h == 720 && d[2].scale == 50);
    EXPECT(display_seq_parse("10:borderless:-:-", d, DISPLAY_SEQ_MAX) == 1 && !strcmp(d[0].mode, "borderless"));
    /* other keys after the scale */
    n = display_seq_parse("10:-:-:-:aspect=4:3;widescreen=on,20:window:1280x720:100", d, DISPLAY_SEQ_MAX);
    EXPECT(n == 2 && !strcmp(d[0].set, "aspect=4:3;widescreen=on") && d[0].scale == 0 && d[1].set[0] == 0);
    EXPECT(display_seq_parse("10:-:-:-:aspect", d, DISPLAY_SEQ_MAX) == -1);
    /* anything it cannot read takes nothing */
    EXPECT(display_seq_parse("10:tv:-:-", d, DISPLAY_SEQ_MAX) == -1);
    EXPECT(display_seq_parse("10:-:100x100:-", d, DISPLAY_SEQ_MAX) == -1);
    EXPECT(display_seq_parse("10:-:-:1000", d, DISPLAY_SEQ_MAX) == -1);
    EXPECT(display_seq_parse("10:-:-", d, DISPLAY_SEQ_MAX) == -1);
    EXPECT(display_seq_parse("10:-:-:100;20:-:-:50", d, DISPLAY_SEQ_MAX) == -1);
    EXPECT(display_seq_parse("10:-:-:100,20:-:-:50", d, 1) == -1);
    if (fails) return 1;
    puts("test_display_seq: ok");
    return 0;
}
