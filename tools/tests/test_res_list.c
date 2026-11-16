/* The Resolution row's sizes (port/kz1hd/res_list.h): the setting's own size is always offered, even on a display
 * that lists more sizes than the row holds (Windows at 3440x1440, 2026-11-15: 1280x720 was cut off, the row showed
 * 3440x1440 and the window stayed 1280x720). */
#include <stdio.h>
#include "res_list.h"

static int has(int (*l)[2], int c, int w, int h) { for (int i = 0; i < c; i++) if (l[i][0] == w && l[i][1] == h) return 1; return 0; }
static int sorted(int (*l)[2], int c) { for (int i = 1; i < c; i++) if (l[i - 1][0] * l[i - 1][1] < l[i][0] * l[i][1]) return 0; return 1; }

int main(void)
{
    int fails = 0;
#define EXPECT(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)
    /* a 3440x1440 display's sizes, largest first, as Windows lists them: 20, 1280x720 the 17th */
    static const int win[][2] = { {3440,1440},{2560,1440},{2560,1080},{1920,1440},{1920,1200},{1920,1080},
        {1680,1050},{1600,1200},{1600,1024},{1600,900},{1440,900},{1400,1050},{1366,768},{1360,768},{1280,1024},
        {1280,960},{1280,720},{1024,768},{800,600},{640,480} };
    int out[16][2];
    int c = kz_res_list(win, 20, 1280, 720, out, 16);
    EXPECT(c == 16, "a long list is cut to the row's 16");
    EXPECT(has(out, c, 1280, 720), "the setting's 1280x720 is offered though it was the 17th");
    EXPECT(out[0][0] == 3440 && out[0][1] == 1440, "the native size stays first");
    EXPECT(out[15][0] == 1280 && out[15][1] == 720, "it goes in last, the smallest of them");
    EXPECT(!has(out, c, 1280, 960), "the 16th listed size gave way to it");

    c = kz_res_list(win, 20, 2560, 1080, out, 16);
    EXPECT(c == 16 && has(out, c, 2560, 1080) && out[2][0] == 2560 && out[2][1] == 1080, "a listed setting keeps its place");
    c = kz_res_list(win, 5, 1000, 700, out, 16);
    EXPECT(c == 6 && has(out, c, 1000, 700) && sorted(out, c), "a size the display does not list is added in order");
    c = kz_res_list(win, 5, 0, 0, out, 16);
    EXPECT(c == 5, "a setting that does not parse adds nothing");
    c = kz_res_list(win, 0, 3440, 1440, out, 16);
    EXPECT(c == 6 && has(out, c, 1920, 1080) && out[1][0] == 3440 && sorted(out, c), "no display sizes: the common ones and the setting's, in order");
    if (fails) return 1;
    puts("test_res_list: ok");
    return 0;
}
