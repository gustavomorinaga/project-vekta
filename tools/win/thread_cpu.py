"""Per-thread CPU of a running process on Windows (diagnostic, not for merge).

    python thread_cpu.py <pid> [interval_s=10] [samples=0 (until the process ends)] [top=25]

Every interval it prints one block: the process's CPU (100 % = one logical CPU)
and its busiest threads, by the name the runtime gives them
(ps3_thread_name -> SetThreadDescription: "ppu main", "spu ...", "frame clock").
A thread near 100 % that should be waiting is a busy loop. Read-only: it opens
threads with THREAD_QUERY_LIMITED_INFORMATION and changes nothing.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)

TH32CS_SNAPTHREAD = 0x4
THREAD_QUERY_LIMITED_INFORMATION = 0x0800


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG),
                ("dwFlags", wt.DWORD)]


k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
k32.Thread32First.argtypes = [wt.HANDLE, ctypes.POINTER(THREADENTRY32)]
k32.Thread32Next.argtypes = [wt.HANDLE, ctypes.POINTER(THREADENTRY32)]
k32.OpenThread.restype = wt.HANDLE
k32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.GetThreadTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(wt.FILETIME)] * 4
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.LocalFree.argtypes = [ctypes.c_void_p]
_get_desc = getattr(k32, "GetThreadDescription", None)
if _get_desc:
    _get_desc.argtypes = [wt.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
    _get_desc.restype = ctypes.c_long


def _ft(f):
    return (f.dwHighDateTime << 32) | f.dwLowDateTime


def threads_of(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    out = []
    if not snap or snap == wt.HANDLE(-1).value:
        return out
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            out.append((te.th32ThreadID, te.tpBasePri))
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    return out


def sample(pid):
    """{tid: (name, base priority, user 100ns, kernel 100ns)}"""
    res = {}
    for tid, pri in threads_of(pid):
        h = k32.OpenThread(THREAD_QUERY_LIMITED_INFORMATION, False, tid)
        if not h:
            continue
        c, e, kt, ut = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
        name = ""
        if k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut)):
            if _get_desc:
                p = ctypes.c_void_p()
                if _get_desc(h, ctypes.byref(p)) >= 0 and p.value:
                    name = ctypes.wstring_at(p.value)
                    k32.LocalFree(p)
            res[tid] = (name, pri, _ft(ut), _ft(kt))
        k32.CloseHandle(h)
    return res


def main():
    pid = int(sys.argv[1])
    interval = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    samples = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    top = int(sys.argv[4]) if len(sys.argv) > 4 else 25
    prev, t_prev = sample(pid), time.perf_counter()
    n = 0
    while samples == 0 or n < samples:
        time.sleep(interval)
        cur, t = sample(pid), time.perf_counter()
        if not cur:
            print("process gone")
            return
        wall = (t - t_prev) * 1e7   # 100 ns units
        rows, total = [], 0.0
        for tid, (name, pri, u, k) in cur.items():
            if tid not in prev:
                continue
            du, dk = u - prev[tid][2], k - prev[tid][3]
            pct = (du + dk) / wall * 100.0
            total += pct
            rows.append((pct, du / wall * 100.0, dk / wall * 100.0, tid, pri, name))
        rows.sort(reverse=True)
        n += 1
        print(f"=== {time.strftime('%H:%M:%S')} sample {n}: process {total:.0f} % ({len(cur)} threads)")
        for pct, up, kp, tid, pri, name in rows[:top]:
            print(f"  {pct:6.1f} %  (user {up:5.1f}, kernel {kp:5.1f})  tid {tid:6d}  pri {pri:2d}  {name or '?'}")
        sys.stdout.flush()
        prev, t_prev = cur, t


if __name__ == "__main__":
    main()
