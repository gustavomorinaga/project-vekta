/* Which NV0039 copies run (patch 2059): with the GPU MLAA on and PS3RECOMP_NV0039 off, only Killzone HD's frame
 * chain (the 1280x720x4 frame between a local display buffer and main memory, out and back) goes to the MLAA or is
 * dropped; the copy of the draw target into a screen's background texture (VICTORY, FAILED, pause: local to local)
 * runs. Before the patch every copy was dropped in that mode, and those backgrounds showed whatever the memory held. */
#include <stdio.h>
#include "gcm_nv0039_route.h"

static int fails;
static void expect(const char* what, int got, int want)
{
    static const char* name[] = { "copy", "MLAA", "drop" };
    printf("%-58s %s\n", what, name[got]);
    if (got != want) { fprintf(stderr, "FAIL: %s: %s, expected %s\n", what, name[got], name[want]); fails++; }
}

int main(void)
{
    const unsigned frame = 1280u * 720u * 4u, L = 0, M = 1;
    /* the default: KZ_MLAA=gpu */
    expect("MLAA on: screen snapshot, local -> local, one line", gcm_nv0039_route(0, 1, L, L, frame, 1), NV0039_ROUTE_COPY);
    expect("MLAA on: screen snapshot, local -> local, 720 rows", gcm_nv0039_route(0, 1, L, L, 5120, 720), NV0039_ROUTE_COPY);
    expect("MLAA on: frame out to main memory", gcm_nv0039_route(0, 1, L, M, frame, 1), NV0039_ROUTE_MLAA);
    expect("MLAA on: SPU result back from main memory", gcm_nv0039_route(0, 1, M, L, frame, 1), NV0039_ROUTE_DROP);
    expect("MLAA on: a small copy to main memory", gcm_nv0039_route(0, 1, L, M, 4096, 1), NV0039_ROUTE_COPY);
    /* KZ_MLAA=spu: PS3RECOMP_NV0039=1, everything copied */
    expect("NV0039 on: frame out", gcm_nv0039_route(1, 0, L, M, frame, 1), NV0039_ROUTE_COPY);
    expect("NV0039 on: frame back", gcm_nv0039_route(1, 0, M, L, frame, 1), NV0039_ROUTE_COPY);
    expect("NV0039 on: screen snapshot", gcm_nv0039_route(1, 0, L, L, frame, 1), NV0039_ROUTE_COPY);
    /* both off: the chain is dropped, the snapshot still copied */
    expect("both off: frame out", gcm_nv0039_route(0, 0, L, M, frame, 1), NV0039_ROUTE_DROP);
    expect("both off: screen snapshot", gcm_nv0039_route(0, 0, L, L, frame, 1), NV0039_ROUTE_COPY);
    if (fails) return 1;
    puts("test_nv0039_route: ok");
    return 0;
}
