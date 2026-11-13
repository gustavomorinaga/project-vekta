/* The post-processing strengths (port/kz1hd/post.h): a setting's text to 0-100 % in steps of 10 (clamped, rounded
 * to the step, anything else the default; an older file's on/off), the menu's rows, and the scaling of a game value:
 * at 100 % the very same bits (the game's own picture), at 0 % zero (the game's own "no effect"), in between the
 * value times the strength, rounded to single. Before, motion blur was only on or off and film grain had no setting. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "post.h"

static int fails;
static void eq(const char* what, long got, long want)
{
    if (got != want) { fprintf(stderr, "FAIL: %s = %ld, want %ld\n", what, got, want); fails++; }
    else printf("ok   %s = %ld\n", what, got);
}
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float from_bits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

int main(void)
{
    /* every step reads back as itself */
    for (int v = 0; v <= 100; v += 10) {
        char b[8], w[32];
        snprintf(b, sizeof b, "%d", v);
        snprintf(w, sizeof w, "parse %d", v);
        eq(w, kz_post_parse(b, -1), v);
    }
    /* between steps: rounded to the nearest, 5 up */
    eq("parse 4", kz_post_parse("4", -1), 0);
    eq("parse 5", kz_post_parse("5", -1), 10);
    eq("parse 44", kz_post_parse("44", -1), 40);
    eq("parse 45", kz_post_parse("45", -1), 50);
    eq("parse 99", kz_post_parse("99", -1), 100);
    /* out of range: clamped */
    eq("parse -20", kz_post_parse("-20", -1), 0);
    eq("parse 150", kz_post_parse("150", -1), 100);
    eq("parse 100000", kz_post_parse("100000", -1), 100);
    /* not a number: the default */
    eq("parse empty", kz_post_parse("", 100), 100);
    eq("parse null", kz_post_parse(NULL, 70), 70);
    eq("parse abc", kz_post_parse("abc", 100), 100);
    eq("parse 50%", kz_post_parse("50%", 100), 100);
    eq("parse 0.5", kz_post_parse("0.5", 100), 100);
    eq("parse 5 0", kz_post_parse("5 0", 100), 100);
    /* an older file's motion_blur */
    eq("parse on", kz_post_parse("on", -1), 100);
    eq("parse off", kz_post_parse("off", -1), 0);
    /* the menu's rows: 0 = 0 %, 10 = 100 %, out of range clamped */
    eq("row of 0", kz_post_row(0), 0);
    eq("row of 70", kz_post_row(70), 7);
    eq("row of 100", kz_post_row(100), 10);
    eq("row of 130", kz_post_row(130), 10);
    eq("from row 3", kz_post_from_row(3), 30);
    eq("from row 10", kz_post_from_row(10), 100);
    eq("from row -1", kz_post_from_row(-1), 0);
    eq("from row 12", kz_post_from_row(12), 100);
    for (int r = 0; r <= 10; r++) {
        char w[32];
        snprintf(w, sizeof w, "row round trip %d", r);
        eq(w, kz_post_row(kz_post_from_row(r)), r);
    }
    /* 100 %: the same bits, whatever they are (the game's own value, untouched) */
    {
        const uint32_t game[] = { 0x3F800000u, 0x3E4CCCCDu, 0x00000001u, 0x80000000u, 0x7FC00001u, 0xFF800000u,
                                  0x42C80000u, 0x3C008081u };
        for (unsigned i = 0; i < sizeof game / sizeof game[0]; i++) {
            char w[48];
            snprintf(w, sizeof w, "100%% keeps 0x%08X", game[i]);
            eq(w, (long)bits(kz_post_scale(from_bits(game[i]), 100)), (long)game[i]);
        }
    }
    /* 0 %: zero, under the grain's 2/255 and the motion field's factor */
    eq("0% of 0.8", (long)bits(kz_post_scale(0.8f, 0)), (long)bits(0.0f));
    /* in between: the value times the strength, in single */
    eq("50% of 0.8", (long)bits(kz_post_scale(0.8f, 50)), (long)bits((float)(0.8f * 0.5)));
    eq("30% of 1.0", (long)bits(kz_post_scale(1.0f, 30)), (long)bits(0.3f));
    eq("10% of 255", (long)lroundf(kz_post_scale(255.0f, 10) * 10.0f), 255);
    /* monotonic across the steps */
    {
        float prev = -1.0f;
        int ok = 1;
        for (int p = 0; p <= 100; p += 10) { const float v = kz_post_scale(0.37f, p); ok &= v > prev; prev = v; }
        eq("steps increase", ok, 1);
    }
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("all post strength checks passed\n");
    return 0;
}
