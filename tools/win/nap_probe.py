"""How long short waits really take on this Windows machine (diagnostic, not for merge).

    python nap_probe.py [iterations=300]

For requested waits of 30, 100, 250, 500 and 1000 us it times three ways to wait:
  hires   a CREATE_WAITABLE_TIMER_HIGH_RESOLUTION timer (Windows 10 1803 and later),
          the nap patch 2058 uses for the flip wait and KZ_FRAME_NAP=1 for the frame clock
  sleep1  Sleep(1), what a millisecond wait is today (the port sets timeBeginPeriod(1) itself,
          so this script does too)
  spin    SwitchToThread until a QueryPerformanceCounter deadline, what sys_timer_usleep does
          today below 1 ms
and prints the actual duration's p50, p90, p99 and max in us, and the CPU the waiting
thread used, as a share of the wall time (100 % = it never slept). Changes nothing; the
timer resolution request ends with the script.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
winmm = ctypes.WinDLL("winmm")
k32.CreateWaitableTimerExW.restype = wt.HANDLE
k32.CreateWaitableTimerExW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, wt.DWORD, wt.DWORD]
k32.SetWaitableTimer.argtypes = [wt.HANDLE, ctypes.POINTER(ctypes.c_longlong), wt.LONG, ctypes.c_void_p,
                                 ctypes.c_void_p, wt.BOOL]
k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
k32.GetCurrentThread.restype = wt.HANDLE
k32.GetThreadTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(wt.FILETIME)] * 4
TIMER_ALL_ACCESS = 0x1F0003
HIGH_RES = 0x00000002


def cpu_100ns():
    c, e, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
    k32.GetThreadTimes(k32.GetCurrentThread(), ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
    return ((k.dwHighDateTime << 32) | k.dwLowDateTime) + ((u.dwHighDateTime << 32) | u.dwLowDateTime)


def run(name, wait, us, n):
    ds = []
    c0, t0 = cpu_100ns(), time.perf_counter_ns()
    for _ in range(n):
        a = time.perf_counter_ns()
        wait(us)
        ds.append((time.perf_counter_ns() - a) / 1000.0)
    wall = (time.perf_counter_ns() - t0) / 100.0
    cpu = (cpu_100ns() - c0) / wall * 100.0 if wall else 0.0
    ds.sort()

    def q(p):
        return ds[min(len(ds) - 1, int(p * len(ds)))]
    print(f"  {name:7s} {us:5d} us: p50 {q(.5):8.1f}  p90 {q(.9):8.1f}  p99 {q(.99):8.1f}  max {ds[-1]:8.1f} us"
          f"   cpu {cpu:5.1f} %")


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    winmm.timeBeginPeriod(1)
    timer = k32.CreateWaitableTimerExW(None, None, HIGH_RES, TIMER_ALL_ACCESS)
    print(f"high-resolution timer: {'yes' if timer else 'no (error %d)' % ctypes.get_last_error()}")

    def hires(us):
        due = ctypes.c_longlong(-us * 10)
        if timer and k32.SetWaitableTimer(timer, ctypes.byref(due), 0, None, None, False):
            k32.WaitForSingleObject(timer, 100)

    def sleep1(us):
        k32.Sleep(1)

    def spin(us):
        end = time.perf_counter_ns() + us * 1000
        while time.perf_counter_ns() < end:
            k32.SwitchToThread()

    for us in (30, 100, 250, 500, 1000):
        print(f"requested {us} us")
        if timer:
            run("hires", hires, us, n)
        run("spin", spin, us, n)
        if us >= 500:
            run("sleep1", sleep1, us, min(n, 200))
    winmm.timeEndPeriod(1)


if __name__ == "__main__":
    main()
