/*
 * KZ_PROF's settings and its trigger file (prof.cpp). Header-only, plain C,
 * so a host test can check them (tools/tests/test_prof_trigger.c).
 *
 * KZ_PROF="<start>,<seconds>[,<out file>]":
 *   <start> seconds after start-up, or 0 for when gameplay is on screen;
 *   "key": when the player presses F11 in the game's window, or when the
 *   trigger file appears (KZ_PROF_TRIGGER=<file>; default <out file>.start),
 *   so a stretch reached only by playing can be recorded without guessing
 *   when it comes. Each trigger records <seconds> once.
 */
#ifndef KZ_PROF_TRIGGER_H
#define KZ_PROF_TRIGGER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct prof_spec {
    int    on_key;     /* wait for F11 or the trigger file */
    double start;      /* seconds; 0 = when gameplay is on screen */
    double secs;       /* how long to record */
    char   out[512];   /* the samples' file */
};

/* 1 when `e` (KZ_PROF's value) was understood. */
static inline int prof_parse(const char* e, struct prof_spec* p)
{
    p->on_key = 0; p->start = 0; p->secs = 30;
    snprintf(p->out, sizeof p->out, "%s", "kz_prof.txt");
    if (!e || !*e) return 0;
    const char* rest = e;
    if (strncmp(e, "key", 3) == 0 && (e[3] == ',' || e[3] == 0)) { p->on_key = 1; rest = e + 3; }
    else {
        char* end;
        p->start = strtod(e, &end);
        if (end == e || p->start < 0) return 0;
        rest = end;
    }
    if (*rest == ',') {
        char* end;
        const double s = strtod(rest + 1, &end);
        if (end == rest + 1 || !(s > 0)) return 0;
        p->secs = s;
        rest = end;
        if (*rest == ',' && rest[1]) snprintf(p->out, sizeof p->out, "%s", rest + 1);
    }
    return 1;
}

/* The trigger file: KZ_PROF_TRIGGER, else <out>.start. */
static inline void prof_trigger_path(const struct prof_spec* p, const char* env, char* path, size_t n)
{
    if (env && *env) snprintf(path, n, "%s", env);
    else snprintf(path, n, "%s.start", p->out);
}

/* 1 (and the file removed, so it can be created again) when the trigger file exists. */
static inline int prof_trigger_fired(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    remove(path);
    return 1;
}
#endif
