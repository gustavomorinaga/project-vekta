"""Where a busy thread of a running process spends its time, on Windows (diagnostic, not for merge).

    python thread_where.py <pid> <tid> [samples=200] [interval_ms=20]

Each sample pauses the thread for a few microseconds (SuspendThread), reads its
instruction pointer and the top 4 KB of its stack, and lets it go on
(ResumeThread). It prints the most frequent instruction pointers and the most
frequent return addresses found on the stack, as module+offset, so a thread
from thread_cpu.py that should be waiting can be named: an offset in
KZ1HD.exe goes through llvm-symbolizer, e.g.
    llvm-symbolizer --obj=KZ1HD.exe --adjust-vma=<image base> 0x<offset>
(the image base is printed). The stack scan is a heuristic: every word that
falls in a module's code counts, so callers show up with some noise; the
callers that appear in nearly every sample are the ones that matter.
It changes nothing in the process beyond those short pauses.
"""
import collections
import ctypes
import ctypes.wintypes as wt
import sys
import time

from thread_cpu import k32, modules_of

THREAD_SUSPEND_RESUME = 0x0002
THREAD_GET_CONTEXT = 0x0008
THREAD_QUERY_INFORMATION = 0x0040
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
CONTEXT_AMD64_CONTROL_INTEGER = 0x00100003
CONTEXT_SIZE = 0x4D0
OFF_FLAGS, OFF_RSP, OFF_RIP = 0x30, 0x98, 0xF8

k32.SuspendThread.argtypes = [wt.HANDLE]
k32.SuspendThread.restype = wt.DWORD
k32.ResumeThread.argtypes = [wt.HANDLE]
k32.ResumeThread.restype = wt.DWORD
k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]


def where(addr, mods):
    for base, size, name in mods:
        if base <= addr < base + size:
            return name, addr - base
    return None, addr


def main():
    pid, tid = int(sys.argv[1]), int(sys.argv[2])
    samples = int(sys.argv[3]) if len(sys.argv) > 3 else 200
    interval = (float(sys.argv[4]) if len(sys.argv) > 4 else 20.0) / 1000.0
    mods = modules_of(pid)
    for base, size, name in mods:
        if name.lower().endswith(".exe"):
            print(f"image base of {name}: 0x{base:X}")
    hp = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    ht = k32.OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, False, tid)
    if not hp or not ht:
        print(f"cannot open process/thread (error {ctypes.get_last_error()})")
        return
    raw = ctypes.create_string_buffer(CONTEXT_SIZE + 16)
    ctx = (ctypes.addressof(raw) + 15) & ~15
    stack = ctypes.create_string_buffer(4096)
    ips, rets, done = collections.Counter(), collections.Counter(), 0
    for _ in range(samples):
        ctypes.memset(ctx, 0, CONTEXT_SIZE)
        ctypes.c_uint32.from_address(ctx + OFF_FLAGS).value = CONTEXT_AMD64_CONTROL_INTEGER
        if k32.SuspendThread(ht) == 0xFFFFFFFF:
            print("thread gone")
            break
        got = 0
        try:
            if k32.GetThreadContext(ht, ctx):
                rip = ctypes.c_uint64.from_address(ctx + OFF_RIP).value
                rsp = ctypes.c_uint64.from_address(ctx + OFF_RSP).value
                n = ctypes.c_size_t()
                got = k32.ReadProcessMemory(hp, ctypes.c_void_p(rsp), stack, 4096, ctypes.byref(n))
        finally:
            k32.ResumeThread(ht)
        if not got:
            time.sleep(interval)
            continue
        done += 1
        ips[where(rip, mods)] += 1
        seen = set()
        for i in range(0, n.value - 7, 8):
            w = int.from_bytes(stack.raw[i:i + 8], "little")
            m, off = where(w, mods)
            if m and (m, off) not in seen:
                seen.add((m, off))
                rets[(m, off)] += 1
        time.sleep(interval)
    k32.CloseHandle(ht)
    k32.CloseHandle(hp)
    print(f"{done} samples of thread {tid}")
    print("instruction pointer:")
    for (m, off), c in ips.most_common(12):
        print(f"  {100.0 * c / max(done, 1):5.1f} %  {m or '?'}+0x{off:X}")
    print("on the stack (return addresses and other code pointers):")
    for (m, off), c in rets.most_common(30):
        print(f"  {100.0 * c / max(done, 1):5.1f} %  {m}+0x{off:X}")


if __name__ == "__main__":
    main()
