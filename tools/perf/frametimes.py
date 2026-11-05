#!/usr/bin/env python3
"""Present intervals and soak tests of the recompiled Killzone 1 HD (port/kz1hd/perf.cpp).

  frametimes.py show <frames.txt> [<other.txt>]   percentiles and histogram of each file
                                                  (all windows together); with two, the change
  frametimes.py soak <dir>                         a tools/perf/soak.sh run: memory growth per
                                                  hour and fps per loop

A frames file is what KZ_PERF_OUT appends: "# <tag>" and one interval in ms per line.
"""
import re
import sys

EDGES = [4.17, 6.06, 6.94, 8.33, 11.1, 16.7, 33.3]   # 240, 165, 144, 120, 90, 60, 30 Hz


def load(path):
    t = []
    with open(path) as f:
        for line in f:
            if line[:1] not in ('#', '\n'):
                t.append(float(line))
    return t


def stats(t):
    s = sorted(t)
    n = len(s)

    def pct(p):
        return s[min(n - 1, int(p * (n - 1) + 0.5))]

    def low(frac):
        k = max(1, int(n * frac))
        return 1000.0 * k / sum(s[n - k:])

    med = pct(0.5)
    return {'n': n, 'fps': 1000.0 * n / sum(s), 'p50': med, 'p90': pct(0.9), 'p99': pct(0.99),
            'p99.9': pct(0.999), 'max': s[-1], '1% low': low(0.01), '0.1% low': low(0.001),
            'stutters': sum(1 for x in s if x > 2 * med)}


def histogram(t):
    n = [0] * (len(EDGES) + 1)
    for x in t:
        b = 0
        while b < len(EDGES) and x >= EDGES[b]:
            b += 1
        n[b] += 1
    rows = []
    for b, c in enumerate(n):
        lo = '0' if b == 0 else '%.2f' % EDGES[b - 1]
        hi = '%.2f' % EDGES[b] if b < len(EDGES) else 'inf'
        share = 100.0 * c / len(t)
        rows.append('  %6s-%-6s ms %6.2f%% %s' % (lo, hi, share, '#' * int(share / 2 + 0.5)))
    return rows


def show(paths):
    res = []
    for p in paths:
        t = load(p)
        if not t:
            print('%s: no intervals' % p)
            continue
        s = stats(t)
        res.append(s)
        print('%s: %d frames, %.1f fps; ms p50 %.2f p90 %.2f p99 %.2f p99.9 %.2f max %.2f; '
              '1%% low %.1f fps, 0.1%% low %.1f fps; %d stutters' %
              (p, s['n'], s['fps'], s['p50'], s['p90'], s['p99'], s['p99.9'], s['max'],
               s['1% low'], s['0.1% low'], s['stutters']))
        print('\n'.join(histogram(t)))
    if len(res) == 2:
        a, b = res
        print('change (second vs first): ' + ', '.join(
            '%s %+.1f%%' % (k, 100.0 * (b[k] - a[k]) / a[k])
            for k in ('fps', 'p50', 'p99', 'p99.9', '1% low', '0.1% low') if a[k]))


def slope(xs, ys):
    """least squares, units of y per unit of x"""
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    d = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / d if d else 0.0


def soak(d):
    rows = []
    with open(d + '/samples.csv') as f:
        next(f)
        for line in f:
            rows.append([float(v) for v in line.strip().split(',')])
    if len(rows) < 3:
        print('soak %s: too few samples (%d)' % (d, len(rows)))
        return
    # the first loop's level load grows memory on its own: from the first loop's end on
    start = next((r for r in rows if r[5] >= 1), rows[0])
    rows = [r for r in rows if r[0] >= start[0]] or rows
    hours = [r[0] / 3600.0 for r in rows]
    print('soak %s: %.2f h sampled, %d loops' % (d, (rows[-1][0] - rows[0][0]) / 3600.0, int(rows[-1][5])))
    for i, name, unit in ((1, 'RSS', 'MB'), (4, 'VRAM', 'MB'), (2, 'threads', ''), (3, 'fds', '')):
        ys = [r[i] for r in rows]
        print('  %-7s %9.1f -> %9.1f %s, %+.2f %s/h (least squares)' %
              (name, ys[0], ys[-1], unit, slope(hours, ys), unit or 'per'))
    loops = []
    with open(d + '/game.log', errors='replace') as f:
        for line in f:
            m = re.match(r'\[route\] loop (\d+) \w+: (\d+) frames in [\d.]+ s = ([\d.]+) fps, 1% low ([\d.]+)', line)
            if m:
                loops.append((int(m.group(1)), int(m.group(2)), float(m.group(3)), float(m.group(4))))
    if loops:
        fps = [l[2] for l in loops]
        print('  fps per loop: first %.1f, last %.1f, min %.1f, max %.1f (1%% low first %.1f, last %.1f)' %
              (fps[0], fps[-1], min(fps), max(fps), loops[0][3], loops[-1][3]))
        if len(loops) > 2:
            print('  fps trend %+.2f per loop' % slope([l[0] for l in loops], fps))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'show':
        show(sys.argv[2:4])
    elif len(sys.argv) == 3 and sys.argv[1] == 'soak':
        soak(sys.argv[2])
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == '__main__':
    main()
