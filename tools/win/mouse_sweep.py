"""Move the mouse at a steady rate while a game window has the focus, on Windows (diagnostic, not for merge).

    python mouse_sweep.py <pid> [seconds=20] [counts_per_s=1500] [swing_s=1.5]

Sends relative mouse motion (SendInput, MOUSEEVENTF_MOVE: no buttons, no keys) about every millisecond, left for
swing_s seconds, then right, and so on, but only while the foreground window belongs to process <pid>; with any
other window in front it sends nothing and waits. A route recorded meanwhile (KZ_ROUTE_REC) holds the counts each
frame got ("M dx dy"), which tools/win/route_mouse_stats.py turns into how evenly they arrived.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

u32 = ctypes.WinDLL("user32", use_last_error=True)
winmm = ctypes.WinDLL("winmm")


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("mi", MOUSEINPUT)]


u32.SendInput.argtypes = [wt.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
u32.GetForegroundWindow.restype = wt.HWND
u32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
MOUSEEVENTF_MOVE = 0x0001


def game_in_front(pid):
    h = u32.GetForegroundWindow()
    p = wt.DWORD()
    return bool(h) and u32.GetWindowThreadProcessId(h, ctypes.byref(p)) and p.value == pid


def main():
    pid = int(sys.argv[1])
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
    rate = float(sys.argv[3]) if len(sys.argv) > 3 else 1500.0
    swing = float(sys.argv[4]) if len(sys.argv) > 4 else 1.5
    winmm.timeBeginPeriod(1)
    inp = INPUT(type=0)
    sent, moved, owed, last = 0, 0.0, 0.0, time.perf_counter()
    try:
        while moved < secs:
            time.sleep(0.001)
            now = time.perf_counter()
            dt, last = now - last, now
            if not game_in_front(pid):
                continue
            moved += dt
            owed += rate * dt * (1 if int(moved / swing) % 2 == 0 else -1)
            dx = int(owed)
            if dx:
                owed -= dx
                inp.mi = MOUSEINPUT(dx, 0, 0, MOUSEEVENTF_MOVE, 0, 0)
                sent += abs(dx) if u32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT)) else 0
    finally:
        winmm.timeEndPeriod(1)
    print(f"sent {sent} counts in {moved:.1f} s with the game in front ({sent / max(moved, 1e-9):.0f}/s)")


if __name__ == "__main__":
    main()
