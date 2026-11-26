/* KZ_TEST_DISPLAY_SEQ (testing; pcoptions.cpp): display and internal resolution changes at set times of a run, as a
 * player would make them on the PC page, so a script can check the frame's size after each one.
 *
 *   KZ_TEST_DISPLAY_SEQ="<seconds>:<mode>:<W>x<H>:<scale %>[:<key>=<value>[;<key>=<value>...]],..."
 *
 * The optional last part sets other PC-page keys of kz1hd_pc.ini (aspect, widescreen, ...) as their rows do.
 * mode window | borderless | fullscreen; "-" for a part leaves it as it is (a mode without a size keeps the size
 * last given, the kz1hd_pc.ini one at first). Plain C, header-only: tools/tests/test_display_seq.c checks the parsing. */
#ifndef KZ_DISPLAY_SEQ_H
#define KZ_DISPLAY_SEQ_H
#include <stdio.h>
#include <string.h>

#define DISPLAY_SEQ_MAX 32
struct display_step {
    double at;        /* seconds since the first frame */
    char mode[16];    /* "" = unchanged */
    int w, h;         /* 0 = unchanged */
    int scale;        /* percent, 0 = unchanged */
    char set[64];     /* "key=value;..." or "" */
};

/* The steps in s, in order; returns how many, or -1 for a step it cannot read (none are then taken). */
static inline int display_seq_parse(const char* s, struct display_step* out, int max)
{
    int n = 0;
    while (s && *s) {
        if (n >= max) return -1;
        struct display_step d;
        memset(&d, 0, sizeof d);
        char mode[16], res[24], sc[16];
        int used = 0;
        if (sscanf(s, "%lf:%15[^:]:%23[^:]:%15[^:,]%n", &d.at, mode, res, sc, &used) != 4 || d.at < 0.0) return -1;
        if (s[used] == ':') {
            int more = 0;
            if (sscanf(s + used + 1, "%63[^,]%n", d.set, &more) != 1 || !strchr(d.set, '=')) return -1;
            used += 1 + more;
        }
        if (strcmp(mode, "-")) {
            if (strcmp(mode, "window") && strcmp(mode, "borderless") && strcmp(mode, "fullscreen")) return -1;
            snprintf(d.mode, sizeof d.mode, "%s", mode);
        }
        int end = 0;   /* each part read to its end */
        if (strcmp(res, "-") && (sscanf(res, "%dx%d%n", &d.w, &d.h, &end) != 2 || res[end] || d.w < 320 || d.h < 200))
            return -1;
        if (strcmp(sc, "-") && (sscanf(sc, "%d%n", &d.scale, &end) != 1 || sc[end] || d.scale < 25 || d.scale > 400))
            return -1;
        out[n++] = d;
        s += used;
        if (*s == ',') s++;
        else if (*s) return -1;
    }
    return n;
}
#endif
