# Troubleshooting

By what you see. Each row has the exit code the game or its launcher ends with, what to do, and what to attach to a
bug report. The codes and their one-line texts are `port/kz1hd/kz_exit.h` (the same table is in
[BUILDING.md](BUILDING.md), "Starting from a launcher", and in each release folder's `recomplauncher.json`).

| What you see | Exit code | What to do | What to attach |
|---|---|---|---|
| The launcher says the game is not built yet | 70 | Start Project Vekta again and let it check your files and build (5 to 30 minutes), or run its prepare step (`scripts/prepare.cmd`, `scripts/prepare.sh`). | Nothing yet; if the build then fails, see below. |
| "These files are another edition or version of the game" | 71 | The port works with NPUA80856 (disc or PlayStation Store) and NPEA00418 (European PlayStation Store), version 01.00. The European disc (BCES-01743) has not been tried. [GETTING-FILES.md](GETTING-FILES.md) has the sizes and SHA-256 of each file. | The `DIFFERENT` line of the newest `work/logs/firstrun-*.log`. |
| "A file the port needs is missing or still encrypted" | 72 | Point it at the installed game folder (the one with `PARAM.SFO` and `USRDIR`): `--game DIR`, or the first run's questions (`scripts\run.cmd`, `scripts/run.sh`). An encrypted `EBOOT.BIN` needs your RPCS3 to make the decrypted copy: `--rpcs3`. | The `MISSING` or `WRONG` lines of the newest `work/logs/firstrun-*.log`. |
| "No RPCS3 with the game installed was found" | 73 | Give RPCS3's folder (`--rpcs3 DIR`: where `rpcs3.exe` is, its config folder on Linux, or the game folder RPCS3 installed), or your own files (`--game`, `--eboot`). | Where your RPCS3 is (no files from it). |
| The build stops partway (the bar stops, or the setup shows an error) | 74 | Read the end of the newest `work/logs/firstrun-*.log`: it names the step and the error. Fix what it says (free disk space, a folder path without `[ ] ;`, outside OneDrive, a PC clock that is right), then start again: the build resumes where it stopped. | That log, or the support package (Start menu: "Make a support package"). |
| "This CPU is too old" (or, before 0.6.0, a crash right at the start) | 75 | The build needs x86-64-v3 (AVX2, FMA, BMI2): Intel Haswell or AMD Excavator and later. There is no build for older CPUs in the release; a source build with `KZ_MARCH=""` is untested ([BUILDING.md](BUILDING.md)). | Your CPU's name. |
| "This build was made from another EBOOT.elf" | 76 | The game folder's `EBOOT.elf` is not the one the build was made from (another copy, or a file replaced after the build). Run "Verify and rebuild" (Windows Start menu) or `./project-vekta --prepare` (Linux). | The two `[kz] EBOOT check:` lines of `work/play.log`. |
| "No Vulkan driver was found" | 77 | Install or update your graphics card's driver (on Linux, the distribution's Vulkan driver, such as Mesa's for AMD and Intel). | Your graphics card and driver version. |
| The game closes while playing, and `work/play.log` has a line `[RSX vulkan] device lost in ...` | 78 | The graphics driver stopped responding (on Windows it is reset after 2 s). Update the graphics driver, then play again; if it happens again, report it. On AMD Radeon RX 5000 and 6000 cards under Windows this is being looked at (#129, [KNOWN_ISSUES.md](KNOWN_ISSUES.md)). | The support package (Start menu: "Make a support package"), or `work/play.log`: its `[RSX vulkan] gpu:`, `driver:` and `render:` lines say which GPU, which driver and what it was drawing. |
| "Use my RPCS3 install" fails, and the build log shows RPCS3 ending with -1073741515 (0xC0000135, a DLL not found) | (none) | RPCS3 needs Microsoft's Visual C++ 2015-2022 runtime (x64), which a fresh Windows lacks: install it from [Microsoft's page](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist), check that RPCS3 starts, then run the setup or Project Vekta again. It also needs `vulkan-1.dll`, which your graphics card's driver installs; Windows 10 without one (no driver, or a virtual machine) lacks it, so install or update the driver first. | The newest `work/logs/firstrun-*.log`. |
| Windows 10 cannot open RPCS3's `.7z` download (Explorer or `tar` says "LZMA codec is unsupported") | (none) | Windows 10's own archive tools do not read that format (Windows 11's do): unpack it with [7-Zip](https://www.7-zip.org/), then point Project Vekta at the folder. | Nothing. |
| The PC switches off or restarts while the build compiles | (none) | The build uses every core it can for a few minutes, like any heavy load: a PC that switches off then is too hot or short of power (cooling, dust, power supply). Check that first. To build with fewer cores, set `KZ_JOBS` (for example `KZ_JOBS=4`) before starting the launcher or the setup step, or pass `--jobs 4` to `scripts/run.sh`, `scripts/prepare.sh` or `project-vekta --prepare` (`scripts\run.cmd`, `scripts\prepare.cmd` on Windows). The build resumes where it stopped. | The newest `work/logs/firstrun-*.log` and your CPU and power supply. |
| The build or the setup step stopped after you cancelled it, or after a launcher's time limit | 130 | Start it again: it resumes where it stopped. | Nothing. |
| Wrong arguments | 2 | Run with `--help`: the options are listed in [BUILDING.md](BUILDING.md), "Starting from a launcher". | The command you ran. |
| Anything else (another exit code, a crash such as 0xC0000005, a hang, a black window) | 1 | When the game closes unexpectedly, the launcher says what the exit code means (an access violation, a stack overflow, a lost GPU, a signal on Linux), shows the crash signature when the game wrote one, and offers to make the support package; the last line of `work/play.log` says the same (`[launcher] session ended: ...`). Check [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the signature or the symptom. | The support package (answer yes when the launcher offers it, or make it later: see below). |

## Where the logs are

- `work/play.log`: the last game session (on Windows the game writes it itself; started from Explorer without the
  launcher, `kz1hd.log` beside `KZ1HD.exe`). After the session the launcher writes your home folder as `~` in it,
  folds a run of one repeated line into that line with `[repeated N times]`, and adds the line
  `[launcher] session ended: exit <code> (<meaning>) after <N> s, <kind>` (kinds: clean, quit, crash, hang-killed,
  gpu-lost, killed, failed).
- `work/logs/play-YYYYmmdd-HHMMSS.log`: the earlier sessions, named by when they started: the five newest, and the
  three newest that ended in a crash, a hang or a lost GPU.
- `work/logs/sessions.jsonl`: one line per session: start and end, seconds, exit code, kind, version and the crash
  or hang reports it left. A session with a start and no end did not finish (the PC lost power, or the game or the
  launcher was stopped from outside): the next start says so once.
- `work/crash/`: the crash and hang reports the game writes (`crash-*.txt`, `hang-*.txt`; the launcher tells the game
  this folder with `KZ_CRASH_DIR`). RecompLauncher's "Crash reports" button opens it.
- `work/logs/firstrun-*.log`: the check and the build, the five newest kept, with your home folder written as `~`.
- `work/crash/`: when the game crashes or stops showing new frames, `crash-*.txt` or `hang-*.txt` with where it
  happened (the same `[kz-crash]` or `[kz-hang]` lines are in `play.log`). Attach them with the log.
- **The support package**: one zip for a bug report, on the Desktop. The launcher offers it when the game closes
  unexpectedly; to make it at any time: the Windows Start menu's "Make a support package", `scripts\support.cmd`
  in the Windows zip, `scripts/support.sh` or `./project-vekta --support-package` in the Linux folder
  (`tools/release/launch.py --support-package` from source). It holds `play.log`, the three newest earlier logs,
  `sessions.jsonl`, the ten newest crash and hang reports and a copy of each with the names of the game's functions
  (read from your own build's symbol table), the two newest build logs, your settings and key map, the error reports
  of the last 24 hours that Windows logged for `KZ1HD.exe` and its display driver events (on Linux, the kernel log's
  GPU lines, when your user may read it), `report.txt` if you typed a few words when asked, and `facts.txt` (the
  version, the system, the GPU and driver, how the last session ended, which of these it holds). Your home folder
  is written as `~` and the PC's name is taken out; it holds no saves and no game files, and nothing is sent: you
  attach it. After making it, the launcher offers to open the bug report form on GitHub with the version, the
  system and the crash signature filled in.

## Reporting a problem

Open an issue with the bug template (the launcher can open it filled in) and attach the support package, or what the
row above says. Never attach game files, saves, keys or firmware: the logs never hold them. Known problems with workarounds: [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
