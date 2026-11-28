"""The step of GetTickCount64 and of the performance counter in ms, on Windows (diagnostic, not for merge).

    python tick_probe.py

For one second each, without and then with timeBeginPeriod(1) (the port sets it), it reads GetTickCount64 and a
millisecond clock made from QueryPerformanceCounter in a loop and prints the steps between distinct values. A tick
count that moves in ~15.6 ms steps gates a "poll at most every 4 ms" check to once a step (64 Hz). Changes nothing;
the timer resolution request ends with the script.
"""
import collections
import ctypes
import time

k32 = ctypes.WinDLL("kernel32")
winmm = ctypes.WinDLL("winmm")
k32.GetTickCount64.restype = ctypes.c_ulonglong


def steps(read, secs=1.0):
    seen, last, t_end = collections.Counter(), read(), time.perf_counter() + secs
    while time.perf_counter() < t_end:
        v = read()
        if v != last:
            seen[v - last] += 1
            last = v
    return ", ".join(f"{d} ms x{n}" for d, n in sorted(seen.items()))


def qpc_ms():
    return time.perf_counter_ns() // 1_000_000


def main():
    print(f"GetTickCount64, default resolution: {steps(k32.GetTickCount64)}")
    winmm.timeBeginPeriod(1)
    print(f"GetTickCount64, timeBeginPeriod(1): {steps(k32.GetTickCount64)}")
    print(f"performance counter ms:             {steps(qpc_ms)}")
    winmm.timeEndPeriod(1)


if __name__ == "__main__":
    main()
