/* cellPad's test-input text (libs/input/pad_test_input.h, patch 4019): the PAD_VIRTUAL list, the per-port
 * variable names and a virtual pad's command file. */
#include "pad_test_input.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    char s[16];
    assert(padv_list_item("xbox", 0, s, sizeof s) && !strcmp(s, "xbox"));
    assert(!padv_list_item("xbox", 1, s, sizeof s));
    assert(padv_list_item("xbox, ps5 ,switch", 1, s, sizeof s) && !strcmp(s, "ps5"));
    assert(padv_list_item("xbox, ps5 ,switch", 2, s, sizeof s) && !strcmp(s, "switch"));
    assert(!padv_list_item("xbox,,ps5", 1, s, sizeof s));          /* an empty entry ends the list */
    assert(!padv_list_item(NULL, 0, s, sizeof s) && !padv_list_item("", 0, s, sizeof s));
    assert(padv_list_item("averyveryverylongname", 0, s, 5) && !strcmp(s, "aver"));

    char v[32];
    padv_env_name("PAD_FILE", 0, v, sizeof v);          assert(!strcmp(v, "PAD_FILE"));
    padv_env_name("PAD_FILE", 1, v, sizeof v);          assert(!strcmp(v, "PAD_FILE_1"));
    padv_env_name("PAD_VIRTUAL_FILE", 3, v, sizeof v);  assert(!strcmp(v, "PAD_VIRTUAL_FILE_3"));

    unsigned b = 99; int ax[6] = {9, 9, 9, 9, 9, 9};
    assert(padv_parse("detach\n", &b, ax) == PADV_DETACH);
    assert(padv_parse("  attach", &b, ax) == PADV_ATTACH);
    assert(padv_parse("", &b, ax) == PADV_NONE);
    assert(padv_parse("\n", &b, ax) == PADV_NONE);
    assert(padv_parse("1001 0 -32768 32767 5 32767 0\n", &b, ax) == PADV_STATE);
    assert(b == 0x1001 && ax[0] == 0 && ax[1] == -32768 && ax[2] == 32767 && ax[3] == 5 && ax[4] == 32767 && ax[5] == 0);
    assert(padv_parse("a", &b, ax) == PADV_STATE && b == 0xA);   /* a hex mask, not "attach" */
    for (int i = 0; i < 6; i++) assert(ax[i] == 0);              /* values left out are 0 */
    puts("PASS pad_test_input");
    return 0;
}
