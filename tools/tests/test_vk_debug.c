/* Patch 2064: PS3RECOMP_VK_VALIDATION's parser and the debug messenger's repeat filter (rsx_vk_debug.h). A frame
 * repeats one validation message hundreds of times; the filter must log the first few of each message ID in full,
 * then only a count line at 10, 100, 1000... occurrences, keep IDs apart (by name when the ID is 0), count what does
 * not fit in its table, and list the counts most frequent first. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsx_vk_debug.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

static void test_flag(void)
{
    static const char* on[] = { "1", "on", "ON", "yes", "True" };
    static const char* off[] = { "", "0", "off", "No", "FALSE" };
    static const char* bad[] = { "2", "enable", "o", "1 ", "validation", "yess" };
    CHECK(rsx_vk_env_flag(NULL) == 0, "unset is off");
    for (size_t i = 0; i < sizeof on / sizeof *on; i++) CHECK(rsx_vk_env_flag(on[i]) == 1, "'%s' is on", on[i]);
    for (size_t i = 0; i < sizeof off / sizeof *off; i++) CHECK(rsx_vk_env_flag(off[i]) == 0, "'%s' is off", off[i]);
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(rsx_vk_env_flag(bad[i]) == -1, "'%s' is not understood", bad[i]);
}

static void test_repeats(void)
{
    rsx_vk_msg_filter* f = calloc(1, sizeof *f);
    uint64_t n = 0;
    int logged = 0, counted = 0;
    for (int i = 1; i <= 1000; i++) {
        const int w = rsx_vk_msg_note(f, 0x9805298C, "Undefined-Value-ShaderOutputNotConsumed", 0x100u, &n);
        CHECK(n == (uint64_t)i, "occurrence %d counted as %llu", i, (unsigned long long)n);
        if (i <= RSX_VK_MSG_FULL) CHECK(w == RSX_VK_MSG_LOG, "occurrence %d is logged in full", i);
        else if (i == 10 || i == 100 || i == 1000) CHECK(w == RSX_VK_MSG_COUNT, "occurrence %d gets a count line", i);
        else CHECK(w == RSX_VK_MSG_QUIET, "occurrence %d is quiet (got %d)", i, w);
        logged += w == RSX_VK_MSG_LOG; counted += w == RSX_VK_MSG_COUNT;
    }
    CHECK(logged == RSX_VK_MSG_FULL && counted == 3, "1000 repeats gave %d full lines and %d count lines", logged, counted);
    /* another ID starts over; the first one keeps its count */
    CHECK(rsx_vk_msg_note(f, 0x12345678, "VUID-x", 0x1000u, &n) == RSX_VK_MSG_LOG && n == 1, "a new ID is logged");
    CHECK(rsx_vk_msg_note(f, 0x9805298C, "Undefined-Value-ShaderOutputNotConsumed", 0x100u, &n) == RSX_VK_MSG_QUIET &&
          n == 1001, "the first ID kept its count");
    /* ID 0 (some loader and driver messages): keyed by name */
    rsx_vk_msg_note(f, 0, "Loader Message", 0x10u, &n);
    CHECK(n == 1, "a name with ID 0 is a key of its own");
    rsx_vk_msg_note(f, 0, "Loader Message", 0x10u, &n);
    CHECK(n == 2, "the same name with ID 0 is the same key");
    rsx_vk_msg_note(f, 0, "Other Message", 0x10u, &n);
    CHECK(n == 1, "another name with ID 0 is another key");
    rsx_vk_msg_note(f, 0, NULL, 0x10u, &n);
    CHECK(n == 1, "no name and ID 0 is a key too");
    CHECK(f->used == 5 && f->total == 1006, "5 keys and 1006 messages (got %u, %llu)", f->used,
          (unsigned long long)f->total);
    /* the highest severity seen is kept */
    rsx_vk_msg_note(f, 0x12345678, "VUID-x", 0x100u, &n);
    CHECK(f->slot[1].severity == 0x1000u, "a warning after an error keeps the error");
    free(f);
}

static void test_full_table(void)
{
    rsx_vk_msg_filter* f = calloc(1, sizeof *f);
    uint64_t n = 7;
    for (int i = 1; i <= RSX_VK_MSG_SLOTS; i++) rsx_vk_msg_note(f, i, "x", 0x100u, &n);
    CHECK(f->used == RSX_VK_MSG_SLOTS && f->other == 0, "the table holds %d keys", RSX_VK_MSG_SLOTS);
    CHECK(rsx_vk_msg_note(f, -1, "late", 0x1000u, &n) == RSX_VK_MSG_LOG && n == 0, "the first key with no slot is shown");
    CHECK(rsx_vk_msg_note(f, -2, "later", 0x1000u, &n) == RSX_VK_MSG_QUIET, "later keys with no slot are counted only");
    CHECK(f->other == 2, "2 messages in 'other' (got %llu)", (unsigned long long)f->other);
    rsx_vk_msg_note(f, 5, "x", 0x100u, &n);
    CHECK(n == 2, "a key in the table still counts when it is full");
    free(f);
}

static void test_summary(void)
{
    rsx_vk_msg_filter* f = calloc(1, sizeof *f);
    for (int i = 0; i < 3; i++) rsx_vk_msg_note(f, 0x111, "rare", 0x100u, NULL);
    for (int i = 0; i < 50; i++) rsx_vk_msg_note(f, 0x222, "often", 0x1000u, NULL);
    for (int i = 0; i < 7; i++) rsx_vk_msg_note(f, 0x333, "sometimes", 0x100u, NULL);
    char buf[2048] = {0};
    FILE* m = tmpfile();
    if (!m) { CHECK(0, "tmpfile"); free(f); return; }
    rsx_vk_msg_summary(f, m, "P ");
    rewind(m);
    size_t got = fread(buf, 1, sizeof buf - 1, m);
    fclose(m);
    buf[got] = 0;
    const char* a = strstr(buf, "often"), *b = strstr(buf, "sometimes"), *c = strstr(buf, "rare");
    CHECK(strstr(buf, "P validation: 60 message(s), 3 distinct") != NULL, "summary header:\n%s", buf);
    CHECK(a && b && c && a < b && b < c, "most frequent first:\n%s", buf);
    CHECK(strstr(buf, "50  error    0x00000222  often") != NULL, "a line with count, severity, ID, name:\n%s", buf);
    free(f);
}

int main(void)
{
    test_flag();
    test_repeats();
    test_full_table();
    test_summary();
    if (fails) { fprintf(stderr, "test_vk_debug: %d failure(s)\n", fails); return 1; }
    puts("test_vk_debug: ok");
    return 0;
}
