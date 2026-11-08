/* cellAudio's notify registrations (patch 3033): a key registered by CreateNotifyEventQueue and again by
 * SetNotifyEventQueue holds one slot, and RemoveNotifyEventQueue frees it, so a title that does Create, Set and
 * Remove for every movie (Killzone HD's player) can play any number of them. Before the patch each round leaked a
 * slot and the 8th Set failed: the movie never started and the main menu stayed black after a long session. */
#include <stdio.h>
#include "cellAudio_notify.h"

#define N 8   /* CELL_AUDIO_MAX_NOTIFY_EVENT_QUEUES */

int main(void)
{
    AudioNotifySlot s[N] = { { 0, 0 } };
    /* one key that stays registered the whole time (the game's own audio) */
    if (audio_notify_register(s, N, 0x8000000000000001ull) < 0) { fprintf(stderr, "FAIL: first key\n"); return 1; }
    for (unsigned m = 0; m < 50; m++) {
        const unsigned long long k = 0x8000000000000002ull + m;
        const int created = audio_notify_register(s, N, k);   /* Create */
        const int set = audio_notify_register(s, N, k);       /* Set */
        if (created < 0 || set != created) {
            fprintf(stderr, "FAIL: movie %u: Create slot %d, Set slot %d\n", m + 1, created, set);
            return 1;
        }
        int used = 0;
        for (int i = 0; i < N; i++) used += s[i].in_use && s[i].key == k;
        if (used != 1) { fprintf(stderr, "FAIL: movie %u: its key holds %d slots\n", m + 1, used); return 1; }
        if (!audio_notify_remove(s, N, k)) { fprintf(stderr, "FAIL: movie %u: Remove found nothing\n", m + 1); return 1; }
        if (audio_notify_remove(s, N, k)) { fprintf(stderr, "FAIL: movie %u: removed twice\n", m + 1); return 1; }
    }
    int live = 0;
    for (int i = 0; i < N; i++) live += s[i].in_use;
    if (live != 1) { fprintf(stderr, "FAIL: %d slots in use after 50 movies, expected 1\n", live); return 1; }
    /* the table still fills and reports it */
    for (int i = 0; i < N - 1; i++) audio_notify_register(s, N, 0x9000000000000000ull + (unsigned)i);
    if (audio_notify_register(s, N, 0xA000000000000000ull) != -1) { fprintf(stderr, "FAIL: a 9th key fit\n"); return 1; }
    puts("50 movies of Create, Set, Remove: one slot each, none leaked");
    puts("test_audio_notify: ok");
    return 0;
}
