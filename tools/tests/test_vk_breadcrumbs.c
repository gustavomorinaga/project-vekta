/* Patches 2084 and 2085: the render breadcrumbs (rsx_vk_breadcrumbs.h) and the GPU wait in steps (rsx_vk_gpu_wait.h).
 * The ring keeps the last 64 entries, oldest first, and marks each done, submitted or recording from the backend's
 * submission serials (presents by their own count); its lines fit the buffer whole or not at all and are always
 * terminated, with a label that is long or holds a newline. The wait waits 2 s and then 1 s at a time, says after
 * each step how long it has waited (2, 3, 4... s), never gives up on its own, and stops on any other result, such
 * as a lost device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsx_vk_breadcrumbs.h"
#include "rsx_vk_gpu_wait.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

static int lines(const char* s)
{
    int n = 0;
    for (; *s; s++) n += *s == '\n';
    return n;
}

static void test_lines(void)
{
    rsx_vk_crumbs* r = calloc(1, sizeof *r);
    char buf[RSX_VK_CRUMBS * 300];
    CHECK(rsx_vk_crumbs_format(r, 0, 1, 0, 1, buf, sizeof buf) == 0 && !buf[0], "an empty ring gives no lines");

    rsx_vk_crumb* p = rsx_vk_crumb_add(r, RSX_VK_CRUMB_PASS, 812, 4410, "pass 3: A 0x0C3A0000 640x360 RGBA8");
    rsx_vk_crumb_draw(p, 0x0123456789ABCDEFull, 0x1ull);
    rsx_vk_crumb_draw(p, 0xAull, 0xBull);
    rsx_vk_crumb_add(r, RSX_VK_CRUMB_PORT, 812, 4410, "jm_post MLAA (port, GPU) 1280x720");
    rsx_vk_crumb_add(r, RSX_VK_CRUMB_SUBMIT, 812, 4410, "submit");
    rsx_vk_crumb_add(r, RSX_VK_CRUMB_PASS, 812, 4411, NULL);
    rsx_vk_crumb_add(r, RSX_VK_CRUMB_PRESENT, 812, 790, "present: display image to the window");
    rsx_vk_crumb_add(r, RSX_VK_CRUMB_PASS, 813, 4412, "pass 0: A 0x0C000000 1280x720 RGBA8");
    /* 4410 done, 4411 submitted, 4412 recording; present 790 submitted (789 done, 791 recording) */
    const size_t len = rsx_vk_crumbs_format(r, 4410, 4412, 789, 791, buf, sizeof buf);
    const char* want =
        "frame 812 batch 4410 done: pass 3: A 0x0C3A0000 640x360 RGBA8; 2 draws, last vs 000000000000000a ps 000000000000000b\n"
        "frame 812 batch 4410 done: jm_post MLAA (port, GPU) 1280x720\n"
        "frame 812 batch 4410 done: submit\n"
        "frame 812 batch 4411 submitted: render pass\n"
        "frame 812 present 790 submitted: present: display image to the window\n"
        "frame 813 batch 4412 recording: pass 0: A 0x0C000000 1280x720 RGBA8\n";
    CHECK(!strcmp(buf, want), "the lines:\n%s\nwant:\n%s", buf, want);
    CHECK(len == strlen(buf), "the length is the text's (%zu, %zu)", len, strlen(buf));
    CHECK(rsx_vk_crumbs_format(r, 4412, 4413, 790, 791, buf, sizeof buf) && strstr(buf, "present 790 done") &&
          strstr(buf, "batch 4412 done") && !strstr(buf, "submitted"), "all finished: every entry done");

    /* lines fit whole or are left out, and the text is terminated */
    char small[100];
    memset(small, 'x', sizeof small);
    const size_t sl = rsx_vk_crumbs_format(r, 4410, 4412, 789, 791, small, sizeof small);
    CHECK(sl < sizeof small && small[sl] == 0 && lines(small) == 0, "a first line that does not fit: none (%zu)", sl);
    char mid[150];   /* the first line is 115 bytes, the first two 177 */
    const size_t ml = rsx_vk_crumbs_format(r, 4410, 4412, 789, 791, mid, sizeof mid);
    CHECK(ml < sizeof mid && mid[ml] == 0 && lines(mid) == 1 && mid[ml - 1] == '\n' && !strncmp(mid, want, ml),
          "room for one line: that line, whole (%zu)", ml);
    CHECK(rsx_vk_crumbs_format(r, 0, 1, 0, 1, NULL, 10) == 0 && rsx_vk_crumbs_format(r, 0, 1, 0, 1, buf, 0) == 0,
          "no buffer: nothing written");
    free(r);
}

static void test_ring(void)
{
    rsx_vk_crumbs* r = calloc(1, sizeof *r);
    char t[32];
    for (unsigned i = 0; i < 200; i++) {
        snprintf(t, sizeof t, "e%u", i);
        rsx_vk_crumb_add(r, RSX_VK_CRUMB_PORT, i, i + 1, t);
    }
    static char buf[RSX_VK_CRUMBS * 300];
    rsx_vk_crumbs_format(r, 200, 201, 0, 1, buf, sizeof buf);
    CHECK(lines(buf) == (int)RSX_VK_CRUMBS, "the ring keeps the last %u (got %d lines)", RSX_VK_CRUMBS, lines(buf));
    CHECK(!strncmp(buf, "frame 136 batch 137 done: e136\n", 31), "oldest first: entry 136 opens the list");
    CHECK(strstr(buf, "frame 199 batch 200 done: e199\n") && !strstr(buf, ": e135\n"), "entry 199 closes it, 135 is gone");

    /* a label too long or with a newline: cut, kept on one line */
    char longtext[400];
    memset(longtext, 'L', sizeof longtext - 1);
    longtext[sizeof longtext - 1] = 0;
    longtext[5] = '\n';
    rsx_vk_crumb* c = rsx_vk_crumb_add(r, RSX_VK_CRUMB_PASS, 1, 1, longtext);
    CHECK(strlen(c->text) == RSX_VK_CRUMB_TEXT - 1, "a long label is cut to %u bytes", RSX_VK_CRUMB_TEXT - 1);
    rsx_vk_crumbs_format(r, 1, 2, 0, 1, buf, sizeof buf);
    const char* last = strstr(buf, "LLLLL L");
    CHECK(last && lines(buf) == (int)RSX_VK_CRUMBS, "the newline in a label becomes a space");
    rsx_vk_crumb_draw(NULL, 1, 2);   /* no entry: nothing happens */
    free(r);
}

/* a waiter standing in for vkWaitForFences: times out `timeouts` times, then gives `end` */
typedef struct { int timeouts, end, calls; uint64_t ns[16]; unsigned slow[16]; int nslow; } fake;
static int fake_wait(void* ctx, uint64_t ns)
{
    fake* f = ctx;
    if (f->calls < 16) f->ns[f->calls] = ns;
    f->calls++;
    return f->timeouts-- > 0 ? RSX_VK_WAIT_TIMEOUT : f->end;
}
static void fake_slow(void* ctx, unsigned s)
{
    fake* f = ctx;
    if (f->nslow < 16) f->slow[f->nslow] = s;
    f->nslow++;
}

static void test_wait(void)
{
    fake a = { .timeouts = 0, .end = RSX_VK_WAIT_SUCCESS };
    CHECK(rsx_vk_wait_bounded(fake_wait, fake_slow, &a) == RSX_VK_WAIT_SUCCESS && a.calls == 1 && a.nslow == 0 &&
          a.ns[0] == 2000000000ull, "a quick GPU: one wait of at most 2 s, nothing logged");
    fake b = { .timeouts = 3, .end = RSX_VK_WAIT_SUCCESS };
    CHECK(rsx_vk_wait_bounded(fake_wait, fake_slow, &b) == RSX_VK_WAIT_SUCCESS && b.calls == 4 && b.nslow == 3 &&
          b.slow[0] == 2 && b.slow[1] == 3 && b.slow[2] == 4 && b.ns[1] == 1000000000ull && b.ns[3] == 1000000000ull,
          "a slow GPU: 2 s, then 1 s steps, logged at 2, 3 and 4 s, and the wait goes on to the end");
    fake c = { .timeouts = 2, .end = RSX_VK_WAIT_DEVICE_LOST };
    CHECK(rsx_vk_wait_bounded(fake_wait, fake_slow, &c) == RSX_VK_WAIT_DEVICE_LOST && c.calls == 3 && c.nslow == 2,
          "a lost device ends the wait and is returned");
    fake d = { .timeouts = 0, .end = RSX_VK_WAIT_DEVICE_LOST };
    CHECK(rsx_vk_wait_bounded(fake_wait, NULL, &d) == RSX_VK_WAIT_DEVICE_LOST && d.calls == 1,
          "lost at once: returned at once (no slow callback needed)");
    fake e = { .timeouts = 100, .end = RSX_VK_WAIT_SUCCESS };
    CHECK(rsx_vk_wait_bounded(fake_wait, fake_slow, &e) == RSX_VK_WAIT_SUCCESS && e.calls == 101 && e.nslow == 100,
          "it never gives up on its own (100 timeouts, then the fence)");
}

int main(void)
{
    test_lines();
    test_ring();
    test_wait();
    printf("test_vk_breadcrumbs: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
