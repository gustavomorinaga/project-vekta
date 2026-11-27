// The trophy notices' queue and layout (trophy_notice.cpp draws them). ENHANCEMENT: the PS3 game shows no notice of
// its own; this is the port's card (style "Project Vekta", docs/dev/trophies.md). Header-only and free of the
// renderer, so a host test checks it (tools/tests/test_trophy_notice.cpp).
//
// Sizes are at 1080 window lines and scale with the window's height. A card slides in from the right edge, holds,
// and slides out; up to kShown are on screen, stacked down from the top-right corner, and the rest wait in arrival
// order. A waiting card starts (and plays its sound) when a place frees; the cards below a finished one slide up
// (kMoveMs).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tn {

constexpr int kShown = 3;                               // cards on screen at once
constexpr int kQueue = 16;                              // shortcut: more waiting than that drops the newest
constexpr double kInMs = 250, kHoldMs = 5000, kOutMs = 250, kMoveMs = 200;
constexpr double kTotalMs = kInMs + kHoldMs + kOutMs;
constexpr int kW = 470, kH = 96, kMargin = 56, kGap = 14, kCut = 14;   // at 1080 lines

inline double scale_for(uint32_t win_h) { return win_h ? win_h / 1080.0 : 1.0; }
inline int px(double v, double s) { return (int)std::lround(v * s); }

// How far in from the right a card is at age t (0 hidden at the edge, 1 fully in): eased in and out.
inline double slide(double t)
{
    if (t < 0 || t >= kTotalMs) return 0;
    const double u = t < kInMs ? t / kInMs : t < kInMs + kHoldMs ? 1.0 : (kTotalMs - t) / kOutMs;
    return 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);   // ease-out cubic (in), its mirror (out)
}

// Where a card is in the stack (0 the top place; fractions between places) at now_ms, moving from `from` to `to`
// since t0: eased out over kMoveMs.
inline double stack_pos(double from, int to, double t0, double now_ms)
{
    const double u = kMoveMs > 0 ? std::clamp((now_ms - t0) / kMoveMs, 0.0, 1.0) : 1.0;
    return to + (from - to) * (1.0 - u) * (1.0 - u) * (1.0 - u);
}

struct Placed {
    int slot;      // the entry in the queue
    int x, y;      // the card's top-left in the window
    int w, h;
};

// A fixed queue of entries in arrival order; T is the caller's payload (the card's text and pixels).
template <class T>
struct Queue {
    // start < 0: waiting; place is the stack place it moves to, from where it was at moved (stack_pos)
    struct Entry { bool used; double start; int place; double from, moved; T item; };
    Entry e[kQueue] = {};
    int order[kQueue] = {};   // entry indices, oldest first
    int n = 0;

    // false when full (the store keeps the trophy; only the notice is lost)
    bool push(const T& item)
    {
        if (n == kQueue) return false;
        int i = 0;
        while (e[i].used) i++;
        e[i].used = true;
        e[i].start = -1;
        e[i].item = item;
        order[n++] = i;
        return true;
    }

    // At now_ms: drops finished cards, starts waiting ones while fewer than kShown run (each start is reported to
    // on_start(item), once), and lays the running ones out in a win_w x win_h window. Returns how many are placed.
    template <class F>
    int step(double now_ms, uint32_t win_w, uint32_t win_h, Placed* out, F&& on_start)
    {
        for (int k = 0; k < n;) {
            Entry& x = e[order[k]];
            if (x.start >= 0 && now_ms - x.start >= kTotalMs) {
                x.used = false;
                x.item = T{};
                for (int j = k; j + 1 < n; j++) order[j] = order[j + 1];
                n--;
            } else k++;
        }
        const double s = scale_for(win_h);
        const int w = px(kW, s), h = px(kH, s), m = px(kMargin, s), gap = px(kGap, s);
        int placed = 0;
        for (int k = 0; k < n && placed < kShown; k++) {
            Entry& x = e[order[k]];
            if (x.start < 0) {
                x.start = now_ms;
                x.place = placed; x.from = placed; x.moved = now_ms - kMoveMs;
                on_start(x.item);
            } else if (x.place != placed) {   // a card above left: slide up from where it is now
                x.from = stack_pos(x.from, x.place, x.moved, now_ms);
                x.place = placed; x.moved = now_ms;
            }
            const int in = (int)std::lround(slide(now_ms - x.start) * (w + m));
            const int y = m + (int)std::lround(stack_pos(x.from, x.place, x.moved, now_ms) * (h + gap));
            out[placed] = { order[k], (int)win_w - in, y, w, h };
            placed++;
        }
        return placed;
    }

    T& item(int slot) { return e[slot].item; }
    int waiting() const
    {
        int c = 0;
        for (int k = 0; k < n; k++) c += e[order[k]].start < 0;
        return c;
    }
};

}  // namespace tn
