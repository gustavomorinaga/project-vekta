/* KZ_PROF's settings and trigger file (port/kz1hd/prof_trigger.h): "key" waits for F11 or the trigger file, numbers
 * keep their meaning, a malformed value is refused, and the trigger file fires once and is removed so it can be
 * created again for the next recording. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "prof_trigger.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

int main(void)
{
    struct prof_spec p;
    CHECK(prof_parse("key,90,/tmp/x/prof.txt", &p) && p.on_key && p.secs == 90 && !strcmp(p.out, "/tmp/x/prof.txt"),
          "key,90,<file>: on F11, 90 s, that file");
    CHECK(prof_parse("key", &p) && p.on_key && p.secs == 30 && !strcmp(p.out, "kz_prof.txt"), "key alone: 30 s, kz_prof.txt");
    CHECK(prof_parse("600,90,out.txt", &p) && !p.on_key && p.start == 600 && p.secs == 90, "600,90: after 600 s");
    CHECK(prof_parse("0,30", &p) && !p.on_key && p.start == 0 && !strcmp(p.out, "kz_prof.txt"), "0,30: when gameplay shows");
    CHECK(!prof_parse("keys,10", &p), "keys,10 refused");
    CHECK(!prof_parse("abc", &p), "abc refused");
    CHECK(!prof_parse("key,0", &p), "key,0 refused (no duration)");
    char path[600];
    prof_parse("key,10,/some/dir/p.txt", &p);
    prof_trigger_path(&p, NULL, path, sizeof path);
    CHECK(!strcmp(path, "/some/dir/p.txt.start"), "default trigger file: <out>.start");
    prof_trigger_path(&p, "/t/go", path, sizeof path);
    CHECK(!strcmp(path, "/t/go"), "KZ_PROF_TRIGGER wins");
    char dir[] = "/tmp/kzproftrigXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    snprintf(path, sizeof path, "%s/go", dir);
    CHECK(!prof_trigger_fired(path), "no file: not fired");
    FILE* f = fopen(path, "w"); if (f) fclose(f);
    CHECK(prof_trigger_fired(path), "file created: fired");
    CHECK(!prof_trigger_fired(path), "fired once (the file is removed)");
    f = fopen(path, "w"); if (f) fclose(f);
    CHECK(prof_trigger_fired(path), "created again: fired again");
    rmdir(dir);
    if (fails) return 1;
    puts("test_prof_trigger: ok");
    return 0;
}
