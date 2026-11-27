// The trophy notices' queue and layout (port/kz1hd/trophy_notice.h): arrival order, at most three on screen with the
// rest waiting, the slide-in/hold/slide-out timing, the stacking, the scale with the window, and the slide up when a
// card above leaves. The HLE's side (a blocked unlock is never notified, so never queued) is
// test_trophy_notify.c's.
#include <cstdio>
#include <vector>

#include "trophy_notice.h"

static int fails;
#define EXPECT(c, what) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", what); fails++; } else std::printf("ok   %s\n", what); } while (0)

struct Item { int id = 0; };

int main()
{
    tn::Queue<Item> q;
    std::vector<int> started;
    auto on_start = [&](Item& it) { started.push_back(it.id); };
    tn::Placed p[tn::kShown];

    for (int i = 1; i <= 5; i++) q.push(Item{ i });
    int n = q.step(0, 1920, 1080, p, on_start);
    EXPECT(n == 3 && started == (std::vector<int>{ 1, 2, 3 }), "five arrive: the first three start, in order");
    EXPECT(q.waiting() == 2, "the other two wait");
    EXPECT(q.item(p[0].slot).id == 1 && q.item(p[2].slot).id == 3, "the oldest at the top");
    EXPECT(p[0].x == 1920 && p[0].y == 56, "at the start a card is at the right edge, 56 px from the top");
    EXPECT(p[1].y == 56 + 96 + 14 && p[2].y == 56 + 2 * (96 + 14), "stacked 14 px apart");
    EXPECT(p[0].w == 470 && p[0].h == 96, "470 x 96 at 1080 lines");

    n = q.step(tn::kInMs, 1920, 1080, p, on_start);
    EXPECT(n == 3 && p[0].x == 1920 - 56 - 470, "after the slide-in: 56 px from the right edge");
    EXPECT(started.size() == 3, "a running card does not start again");
    n = q.step(tn::kInMs + tn::kHoldMs / 2, 1920, 1080, p, on_start);
    EXPECT(p[0].x == 1920 - 56 - 470, "held in place");
    q.step(tn::kInMs + tn::kHoldMs + tn::kOutMs / 2, 1920, 1080, p, on_start);
    EXPECT(p[0].x > 1920 - 56 - 470 && p[0].x < 1920, "sliding out");

    n = q.step(tn::kTotalMs, 1920, 1080, p, on_start);
    EXPECT(started == (std::vector<int>{ 1, 2, 3, 4, 5 }), "the first three done: the waiting two start");
    EXPECT(n == 2 && q.item(p[0].slot).id == 4 && p[0].y == 56, "the next one takes the top place");
    EXPECT(q.waiting() == 0, "none waits");
    n = q.step(2 * tn::kTotalMs, 1920, 1080, p, on_start);
    EXPECT(n == 0 && q.n == 0, "all done: nothing on screen, the queue empty");

    q.push(Item{ 9 });
    n = q.step(10000, 3840, 2160, p, on_start);
    EXPECT(n == 1 && p[0].w == 940 && p[0].h == 192 && p[0].y == 112, "at 2160 lines everything is twice the size");
    n = q.step(10000 + tn::kInMs, 1280, 720, p, on_start);
    EXPECT(p[0].w == 313 && p[0].h == 64 && p[0].x == 1280 - 37 - 313, "at 720 lines two thirds (rounded)");
    q.step(1e9, 1920, 1080, p, on_start);

    for (int i = 0; i < tn::kQueue; i++) q.push(Item{ 100 + i });
    EXPECT(!q.push(Item{ 999 }), "a full queue refuses the next notice");

    tn::Queue<Item> q2;   // a card above leaves
    q2.push(Item{ 20 });
    q2.step(3e9, 1920, 1080, p, on_start);
    q2.push(Item{ 21 });
    n = q2.step(3e9 + 1000, 1920, 1080, p, on_start);
    EXPECT(n == 2 && p[1].y == 56 + 110, "a later card goes under the first");
    n = q2.step(3e9 + tn::kTotalMs + 1, 1920, 1080, p, on_start);
    EXPECT(n == 1 && q2.item(p[0].slot).id == 21 && p[0].y > 56 + 100, "the first leaves: the second starts to move up");
    q2.step(3e9 + tn::kTotalMs + 1 + tn::kMoveMs / 2, 1920, 1080, p, on_start);
    EXPECT(p[0].y > 56 && p[0].y < 56 + 110, "half way up, not a jump");
    q2.step(3e9 + tn::kTotalMs + 1 + tn::kMoveMs, 1920, 1080, p, on_start);
    EXPECT(p[0].y == 56, "after kMoveMs it is at the top");
    EXPECT(tn::slide(-1) == 0 && tn::slide(tn::kTotalMs) == 0 && tn::slide(tn::kInMs) == 1, "slide: hidden, in, gone");

    if (fails) return 1;
    std::puts("test_trophy_notice: ok");
    return 0;
}
