# Troubleshooting

By what you see. Each row has the exit code the game or its launcher ends with, what to do, and what to attach to a
bug report. The codes and their one-line texts are `port/kz1hd/kz_exit.h` (the same table is in
[BUILDING.md](BUILDING.md), "Starting from a launcher", and in each release folder's `recomplauncher.json`).

| What you see | Exit code | What to do | What to attach |
|---|---|---|---|
| The launcher says the game is not built yet | 70 | Start Project Vekta again and let it check your files and build (5 to 30 minutes), or run its prepare step (`scripts/prepare.cmd`, `scripts/prepare.sh`). | Nothing yet; if the build then fails, see below. |
| "These files are another edition or version of the game" | 71 | The port works only with NPUA80856, version 01.00 (disc or PlayStation Store). The European edition (NPEA00418, BCES-01743) is another build. [GETTING-FILES.md](GETTING-FILES.md) has the sizes and SHA-256 of each file. | The `DIFFERENT` line of the newest `work/logs/firstrun-*.log`. |
| "A file the port needs is missing or still encrypted" | 72 | Point it at the installed game folder (the one with `PARAM.SFO` and `USRDIR`): `--game DIR`, or the first run's questions (`scripts\run.cmd`, `scripts/run.sh`). An encrypted `EBOOT.BIN` needs your RPCS3 to make the decrypted copy: `--rpcs3`. | The `MISSING` or `WRONG` lines of the newest `work/logs/firstrun-*.log`. |
| "No RPCS3 with the game installed was found" | 73 | Give RPCS3's folder (`--rpcs3 DIR`: where `rpcs3.exe` is, its config folder on Linux, or the game folder RPCS3 installed), or your own files (`--game`, `--eboot`). | Where your RPCS3 is (no files from it). |
| The build stops partway (the bar stops, or the setup shows an error) | 74 | Read the end of the newest `work/logs/firstrun-*.log`: it names the step and the error. Fix what it says (free disk space, a folder path without `[ ] ;`, outside OneDrive, a PC clock that is right), then start again: the build resumes where it stopped. | That log, or the support package (Start menu: "Make a support package"). |
| "This CPU is too old" (or, before 0.6.0, a crash right at the start) | 75 | The build needs x86-64-v3 (AVX2, FMA, BMI2): Intel Haswell or AMD Excavator and later. There is no build for older CPUs in the release; a source build with `KZ_MARCH=""` is untested ([BUILDING.md](BUILDING.md)). | Your CPU's name. |
| "This build was made from another EBOOT.elf" | 76 | The game folder's `EBOOT.elf` is not the one the build was made from (another copy, or a file replaced after the build). Run "Verify and rebuild" (Windows Start menu) or `./project-vekta --prepare` (Linux). | The two `[kz] EBOOT check:` lines of `work/play.log`. |
| "No Vulkan driver was found" | 77 | Install or update your graphics card's driver (on Linux, the distribution's Vulkan driver, such as Mesa's for AMD and Intel). | Your graphics card and driver version. |
| "Use my RPCS3 install" fails, and the build log shows RPCS3 ending with -1073741515 (0xC0000135, a DLL not found) | (none) | RPCS3 needs Microsoft's Visual C++ 2015-2022 runtime (x64), which a fresh Windows lacks: install it from [Microsoft's page](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist), check that RPCS3 starts, then run the setup or Project Vekta again. It also needs `vulkan-1.dll`, which your graphics card's driver installs; Windows 10 without one (no driver, or a virtual machine) lacks it, so install or update the driver first. | The newest `work/logs/firstrun-*.log`. |
| Windows 10 cannot open RPCS3's `.7z` download (Explorer or `tar` says "LZMA codec is unsupported") | (none) | Windows 10's own archive tools do not read that format (Windows 11's do): unpack it with [7-Zip](https://www.7-zip.org/), then point Project Vekta at the folder. | Nothing. |
| The PC switches off or restarts while the build compiles | (none) | The build uses every core it can for a few minutes, like any heavy load: a PC that switches off then is too hot or short of power (cooling, dust, power supply). Check that first. To build with fewer cores, set `KZ_JOBS` (for example `KZ_JOBS=4`) before starting the launcher or the setup step, or pass `--jobs 4` to `scripts/run.sh`, `scripts/prepare.sh` or `project-vekta --prepare` (`scripts\run.cmd`, `scripts\prepare.cmd` on Windows). The build resumes where it stopped. | The newest `work/logs/firstrun-*.log` and your CPU and power supply. |
| The build or the setup step stopped after you cancelled it, or after a launcher's time limit | 130 | Start it again: it resumes where it stopped. | Nothing. |
| Wrong arguments | 2 | Run with `--help`: the options are listed in [BUILDING.md](BUILDING.md), "Starting from a launcher". | The command you ran. |
| Anything else (another exit code, a crash, a black window) | 1 | Look at the end of `work/play.log` (the game) or of the newest `work/logs/firstrun-*.log` (the build), and at [KNOWN_ISSUES.md](KNOWN_ISSUES.md). | The support package, or `work/play.log` and the newest `work/logs/firstrun-*.log`. |

## Where the logs are

- `work/play.log`: the last game session (on Windows the game writes it itself; started from Explorer without the
  launcher, `kz1hd.log` beside `KZ1HD.exe`).
- `work/logs/firstrun-*.log`: the check and the build, the five newest kept, with your home folder written as `~`.
- `work/crash/`: when the game crashes or stops showing new frames, `crash-*.txt` or `hang-*.txt` with where it
  happened (the same `[kz-crash]` or `[kz-hang]` lines are in `play.log`). Attach them with the log.
- The support package (Windows Start menu: "Make a support package"): one zip of the logs and settings, on the
  Desktop.

## Reporting a problem

Open an issue with the bug template, and attach what the row above says. Never attach game files, saves, keys or
firmware: the logs never hold them. Known problems with workarounds: [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
