# Crash reports, hang reports and the session header

What the game writes to its log (stderr, `work/play.log`) so a crash or a hang can be told apart and located from the
log alone, and how to test it without waiting for a real crash. The code is `port/kz1hd/crash_report.c` and
`port/kz1hd/sys_info.c`; `tools/release/support.py` and the launcher read these lines, so keep the formats.

## The session header

Right after the version line, one line per fact:

```
[kz] Project Vekta 0.7.0 ...
[kz-sys] os: Windows 11 (10.0.22631)
[kz-sys] cpu: AMD Ryzen 7 7800X3D 8-Core Processor
[kz-sys] cores: 8 cores, 16 threads
[kz-sys] memory: 31.2 GB, 22.9 GB available
[kz-sys] env: KZ_CRASH_DIR="~/Project Vekta/work/crash" KZ_STARTED_BY=setup
[kz-sys] started-by: setup
[kz-sys] settings: anisotropic=16 antialiasing=on ... vsync=fifo
[kz-sys] dlss: yes
```

- `os`: Windows from `RtlGetVersion`, with ` under Wine <version> on <host>` under Wine or Proton; Linux from
  `/etc/os-release` and `uname`.
- `env`: the `KZ_*` and `PS3*` variables set when the game started, before `kz1hd_pc.ini` adds its own; `settings`:
  `kz1hd_pc.ini` as read at start. Both are `name=value` pairs, sorted, space separated; a value with a space is in
  double quotes, and the home folder at the start of a value is written `~`.
- `started-by`: `KZ_STARTED_BY` (the launcher sets `setup`, `zip`, `recomplauncher`, `quiver` or `source`), else
  `unknown`.
- `display` lines come later, once the window exists (SDL's video runs on the frame clock's thread), one per
  monitor: `2560x1440@144Hz scale 125%`. The scale is the monitor's DPI over 96 and is written on Windows only; on
  Linux there is no one answer (X11's DPI is not the desktop's scale).
- The GPU and driver lines come from the Vulkan backend (`[RSX vulkan] driver: ...`).

## The crash report

An unhandled fault on any thread, or `abort()` (`std::terminate`, a failed assert):

```
[kz-crash] begin
[kz-crash] exception: 0xC0000005 EXCEPTION_ACCESS_VIOLATION        (Linux: signal: 11 SIGSEGV)
[kz-crash] address: 0x00007ff6a1b2c3d4
[kz-crash] access: write 0x0000000000000000
[kz-crash] module: KZ1HD.exe+0x2c3d4
[kz-crash] thread: 4242 spu 1:0
[kz-crash] uptime: 72.4 s
[kz-crash] version: Project Vekta 0.7.0 ...
[kz-crash] registers: rip=0x... rsp=0x... rbp=0x... eflags=0x...
[kz-crash] registers: rax=0x... (five lines in all)
[kz-crash] #0 KZ1HD.exe+0x2c3d4
[kz-crash] #1 KZ1HD.exe+0x1a0f00
[kz-crash] signature: KZ-3F2A-91C0
[kz-crash] render: <the last GPU passes, when the backend has them>
[kz-crash] end
```

- `exception`: the code in upper-case hex and its name; Windows' own codes, or `signal: N NAME` on Linux and for
  `abort()` on both (`signal: 22 SIGABRT` on Windows, `6` on Linux). `access` (read, write or execute, and the
  address) only for an access violation. `address` and `module` are where it faulted (for `abort()`, frame #0).
  `registers` are missing for `abort()`, which has no fault context.
- `thread`: the system's thread id and the name the runtime gave the thread (`ppu main`, `spu 1:0`, `audio mix`...;
  the rest of the line), `main` for the main thread, `unnamed` for a thread nobody named.
- Frames: up to 32, `#<n> <module>+0x<offset>`. #0 is the faulting instruction, the rest are return addresses. The
  offset is from the module's base on Windows (an RVA) and the ELF virtual address on Linux (the address less the
  load bias), which is what `addr2line -e`, `nm` and `llvm-symbolizer` take. Nothing is symbolized in the game: the
  lifted functions are named by guest address in the symbol table (`_Z13func_0037DE70P11ppu_context`), so the
  support package can name each frame later on the player's PC.
- `signature`: `KZ-` and 8 hex digits, the FNV-1a hash of the exception code (Linux: the signal number) and the first
  5 frames' `module+offset`, the same from run to run of the same build.
- A copy of the block, without the `[kz-crash] ` prefix, goes to `$KZ_CRASH_DIR/crash-YYYYmmdd-HHMMSS.txt` (local
  time; `-2`, `-3`... if the name is taken) when `KZ_CRASH_DIR` is set; the launcher sets it to `<work>/crash`, and
  the folder is made if it is not there.

How it is installed: `kz_crash_install` in `main()`, before `ppu_run`. `ppu_run`'s `ps3_stdio_init` installs the
runtime's own filter over it later, keeps it as its previous one and calls it between two flushes of the buffered
stdout and stderr, so the log has what the game printed before the crash, then the report, then the rest. The
vectored handlers (`vm_commit_veh`, the guard pages, the write tracking of patch 3031) run first and go on for the
faults they resolve; only what none of them took reaches the report. On Linux the same chain runs from
`win32_compat.c`'s SIGSEGV/SIGBUS/SIGILL/SIGFPE dispatcher.

The report is written line by line with `write()` / `WriteFile` to stderr's descriptor, from fixed buffers, because
the crashing thread may hold the stdio lock. A second thread that faults meanwhile waits; a fault inside the report
ends the process. On Windows the process then ends with the exception code as its exit status, without the Windows
Error Reporting dialog (`KZ_CRASH_WER=1` hands it to Windows instead); on Linux it ends by its signal.

Limits: `__fastfail` (Windows) runs no handler at all, so it has no report (the launcher still sees 0xC0000409). A
stack overflow is reported on the main thread (it has a stack guarantee on Windows and an alternate signal stack on
Linux) but on another thread only on Windows, and only when what is left of its stack is enough; on Linux the
runtime's threads have no alternate signal stack and die without a report. On Linux the unwinder may take the
loader's lock: a crash inside `dlopen` can stop the report after the lines before the frames.

## The hang report

When no new frame came for 5 s (`KZ_HANG_WATCH`) and the window is not minimized, once the game has shown its first
frame (so a long boot or the data step never counts), and again at 30 s; never more than three times a session, and
the game is never stopped:

```
[kz-hang] begin
[kz-hang] no new frame for 5 s
[kz-hang] version: ...
[kz-hang] uptime: 312.0 s
[kz-hang] frames: 8812
[kz-hang] frame loop: last ran 0.0 s ago
[kz-hang] thread: 4242 ppu main
[kz-hang] #0 ntdll.dll+0x9f6a4
[kz-hang] ...
[kz-hang] thread: 4250 frame clock
[kz-hang] ...
[kz-hang] end
```

`frames` is the guest's flip count; `frame loop: last ran` tells a game whose frame loop still runs (the guest is
stuck) from one stuck inside the loop (a present, the driver). Every thread but the watchdog's own follows, with up
to 32 frames each. Windows: a Toolhelp snapshot of the process's threads, each stopped (`SuspendThread`) only for
its registers and a copy of its stack, then let go and unwound on the copy. Linux: each thread in `/proc/self/task`
gets a real-time signal (`SIGRTMIN+6`) whose handler unwinds its own stack; a thread that does not answer in 0.5 s
is listed without frames. A copy goes to `$KZ_CRASH_DIR/hang-YYYYmmdd-HHMMSS.txt`. When frames come again the log
says `[kz] hang watch: frames again after N s`.

## Testing them

- `KZ_CRASH_TEST=<kind>` (default unset) makes the game fail on purpose, `KZ_CRASH_TEST_DELAY` seconds (default 5)
  after start:
  - `null-thread`: a write through a null pointer on a thread of its own (`kz crash test`), while the game runs;
  - `hang`: the frame loop stops for good (no presents, no flips), so the hang reports come at 5 and 30 s;
  - `null-main` (a read through a null pointer), `abort`, `illegal` (an invalid instruction), `stack` (endless
    recursion) and `fastfail` (`__fastfail` on Windows; `SIGKILL` on Linux, an end nothing in the process sees):
    on the main thread, after the delay, after `ps3_stdio_init` and before the game's code starts (the window is
    open).
  `gpu-lost` belongs to the Vulkan diagnostics.
- `KZ_HANG_WATCH=<seconds>` (default 5): the first hang report after that long, the second at six times as long;
  `0` turns the watchdog off.
- `KZ_CRASH_DIR=<folder>` (default unset): where the copies go.
- Host test: `tools/tests/test_crash_report.sh` builds `tools/tests/test_crash_report.c` with these files and
  ps3recomp's `ps3_stdio.c` and `win32_compat.c`, runs every kind in a child process and checks the blocks, the exit
  status, the signature on a second run, the copies, two threads crashing at once, the hang reports (and none with
  `KZ_HANG_WATCH=0` or the window minimized) and the session header; then `addr2line` on frame #0. The Windows build of
  the same program runs under Wine in `tools/wine-host-tests.sh`.
