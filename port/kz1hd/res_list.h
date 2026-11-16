/*
 * The sizes the PC screen's Resolution row offers (pcoptions.cpp): the
 * display's sizes as the Vulkan backend lists them (largest first), or a few
 * common ones when it lists none, at most `max`, and always the setting's
 * own size. Cut to `max` after the setting was added, a display with many
 * modes lost it (Windows at 3440x1440, 2026-11-15: 1280x720 was past the
 * 16th); the row then showed the first entry, the native size, while the
 * window kept the setting's 1280x720, and choosing the size the row already
 * showed changed nothing. Header-only, plain C, so a host test can check it
 * (tools/tests/test_res_list.c).
 */
#ifndef KZ_RES_LIST_H
#define KZ_RES_LIST_H

/* out[i] = {w, h}; returns the count (<= max). cur_w x cur_h is the setting's
 * size, 0x0 when it does not parse. */
static inline int kz_res_list(const int (*modes)[2], int n, int cur_w, int cur_h, int (*out)[2], int max)
{
    static const int common[][2] = { {3840,2160},{2560,1440},{1920,1080},{1600,900},{1280,720} };
    if (max <= 0) return 0;
    if (n <= 0) { modes = common; n = 5; }
    int c = 0;
    for (int i = 0; i < n && c < max; i++) { out[c][0] = modes[i][0]; out[c][1] = modes[i][1]; c++; }
    if (cur_w <= 0 || cur_h <= 0) return c;
    for (int i = 0; i < c; i++) if (out[i][0] == cur_w && out[i][1] == cur_h) return c;
    if (c == max) c--;   /* the smallest listed size gives way to the setting's */
    int k = c++;
    while (k > 0 && out[k - 1][0] * out[k - 1][1] < cur_w * cur_h) { out[k][0] = out[k - 1][0]; out[k][1] = out[k - 1][1]; k--; }
    out[k][0] = cur_w; out[k][1] = cur_h;
    return c;
}
#endif
