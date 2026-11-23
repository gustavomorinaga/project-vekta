# Installing Project Vekta

This page is for players. It describes version <!-- version -->0.6.0<!-- /version -->: the Windows setup program, the Windows zip,
and the Linux folder. Building from a Git clone is in [BUILDING.md](BUILDING.md).

Every download builds the port on your own PC, from your own game. Nothing of the game is in them, and nothing of the
game is downloaded. The first build takes 5 to 30 minutes: about five on a fast
PC with an SSD, nearer half an hour with four cores and a hard disk.

## Before you start

You need these files from your own game. No PS3 system software (firmware) is needed.
[GETTING-FILES.md](GETTING-FILES.md) says what each one must be and lists the tested sizes and SHA-256 sums.

- The installed game folder of **Killzone HD** (NPUA80856, the US edition), from the **Killzone Trilogy** disc or
  bought on the PlayStation Store. It holds `PARAM.SFO`, `TROPDIR` and `USRDIR`. In RPCS3 it is
  `dev_hdd0\game\NPUA80856`. The Store copy's data archive is bound to your licence: use "Use my RPCS3 install",
  which has your RPCS3 open it (with the `.rap` it already uses for the game) into `data_ps3.psarc`.
- `EBOOT.elf`: the game's executable, decrypted. Not `EBOOT.BIN`.
- Optional: `data_ps3.psarc`, the data archive, decrypted, about 3 GB. Without it the port reads the game folder's
  own `USRDIR\data_ps3.psarc.edat`, once, after the build: about a minute more, and 3.1 GB more disk in `work`.

**Already playing it in RPCS3?** When RPCS3 has Killzone HD installed, the Linux folder's
`./project-vekta` and the zip's `Project Vekta.exe` find them and use them without a question (the
first run's questions, `scripts/run.sh` and `scripts\run.cmd`, offer the same ("Use my RPCS3 install", "I have the files
ready", "Show me how", which lists these steps, or "My RPCS3 is somewhere else", which asks for its folder): your
RPCS3 then decrypts a copy of `EBOOT.BIN` into the port's own `work\prep` folder, the data archive is read as it is, and you pick no file. The setup program offers the same on its "Your game" page ("Use my RPCS3
install"), with a "Find RPCS3..." button for an RPCS3 it did not find by itself. Either way the folder can be where
`rpcs3.exe` is, RPCS3's config folder, or the game folder RPCS3 installed (`dev_hdd0\game\NPUA80856`). On a fresh
Windows, RPCS3 itself needs Microsoft's Visual C++ 2015-2022 runtime (x64) before it starts; see
[TROUBLESHOOTING.md](TROUBLESHOOTING.md).

This project gives no decryption tools or instructions. The setup and the scripts only read your files; on the RPCS3
route your own RPCS3 decrypts copies of them, and nothing is written into the game's or RPCS3's folders.

Your PC: Windows x64 or Linux x64, a CPU with AVX2, and a Vulkan graphics card and driver
([requirements.md](requirements.md)). Beside the 4 GB of game files, keep free about 2.4 GB on Windows (3.2 GB
more where Windows cannot make links, so the setup asks for 5.8 GB) or about 1.9 GB on Linux.

Download from the [releases page](https://github.com/gustavomorinaga/project-vekta/releases). Each file has a
`.sha256` beside it, and `SHA256SUMS` lists them all. From 0.7.0 on, each file also has a signed statement of where
it was built (a build provenance attestation, kept by GitHub): with [GitHub's CLI](https://cli.github.com),
`gh attestation verify project-vekta-X.Y.Z-setup.exe --repo gustavomorinaga/project-vekta` says whether this
repository's release workflow built that very file.

## Windows: the setup program

The file is `project-vekta-X.Y.Z-setup.exe`. It installs for your user only and needs no
administrator rights.

1. **Start the setup.** It installs into `%LOCALAPPDATA%\Programs\ProjectVekta` unless you pick another folder. It
   refuses a folder the build cannot use: one inside OneDrive, a drive's root, Windows, Program Files, Documents, the
   Desktop, Downloads or your profile folder, or a path with `[`, `]` or `;` in it.
2. **Pick the tasks.**
   - "Create a desktop shortcut".
   - "Build now, in this wizard": on by default. Untick it to build on the first start instead.
   - "Portable": off by default. Ticked, saves and settings stay in the install folder; left off, they go to
     `%APPDATA%\Project Vekta`. To change it later, run `scripts\play.cmd --move-data portable` (or `--move-data appdata`)
     in the install folder: it moves the saves and settings and remembers the choice, and moves nothing when the
     other folder already has some.
3. **Your game.** "Use my RPCS3 install" when the setup found RPCS3 with the game: the next
   page is skipped. When it did not find yours, press "Find RPCS3..." and pick its folder. "Show me how" lists the steps to get there, and stays on the page. Otherwise, pick the
   installed game folder. Picking the folder above it also works.
4. **Your decrypted files.** Pick `EBOOT.elf`. Leave `data_ps3.psarc` empty to use the game
   folder's own `.edat`, or pick a decrypted one you already have.
5. **NVIDIA DLSS (optional).** The box is unticked by default. Ticking it accepts NVIDIA's DLSS SDK licence, and
   the build then downloads DLSS from NVIDIA's repository. Left unticked, nothing of NVIDIA's is downloaded, and the
   game uses AMD FSR 3.1 as its temporal upscaler.
6. **Install.** Setup checks each file's kind and size before it copies anything. With "Build now", the wizard then
   builds the port with a progress bar and a Stop button. The build's first step checks the files' SHA-256. When the
   build stops, the wizard says why and opens its log.
7. **Play.** At the end, tick "Play Project Vekta now", or start **Project Vekta** from the Start menu or the desktop.
   If the build did not run in the wizard, the first start builds first.

The Start menu also has:

- **Verify and rebuild**: checks the install and your files, then rebuilds what changed.
- **Open saves folder**: opens the folder with your saves and settings.
- **Make a support package**: puts one zip on the Desktop with the logs and settings, for a bug report. It holds no
  saves and no game files, and your user folder is written as `~`.

To install a newer version, run its setup over the old one. The build, your file paths, your saves and your choices
stay. The uninstaller asks whether to delete your saves and settings too; the default is No.

## Windows: the zip

The file is `project-vekta-X.Y.Z-windows.zip`. It is the same folder as the setup program,
without the setup.

1. **Unpack the zip** into a folder of your own, keeping its folders. The zip does not check the folder the way the
   setup does, so avoid the places listed in step 1 above.
2. **Start `Project Vekta.exe`.** When RPCS3 has the game in its usual places, it builds from
   them without a question (go to step 5). Otherwise a console window (`scripts\run.cmd`) asks for each file in
   turn, with a suggested path in brackets. Type or paste a path, or press Enter to keep the one in brackets.
3. **Fix what the check names.** If the check does not pass, the window says which file is wrong and why. Press
   Enter to choose that file again, or `q` to quit.
4. **Answer the DLSS question.** It shows NVIDIA's licence link. `y` accepts it and downloads DLSS; Enter or `n`
   builds without it.
5. **Wait for the build.** A progress bar runs for 5 to 30 minutes. At the end the window says "Built." and
   names the command that plays. The log is in `work\logs\`.
6. **Start `Project Vekta.exe` again** to play (`scripts\play.cmd` does the same in a console; add
   `--fullscreen` for full screen). The settings are in
   `work\vfs\PS3_GAME\USRDIR\kz1hd_pc.ini` and the log in `work\play.log`.

Run `scripts\run.cmd` again after you move your files. A build that was stopped resumes there.
`scripts\run.cmd --dlss accept` or `scripts\run.cmd --dlss decline` changes the DLSS answer.

## Linux: the folder

The file is `project-vekta-X.Y.Z-linux.tar.xz` (a `.tar.gz` up to 0.6.0). It has its own compiler and build tools.
You need Python 3.10 or newer, glibc 2.31 or newer, SDL2 and a Vulkan driver.

1. **Unpack it:** `tar xf project-vekta-X.Y.Z-linux.tar.xz`.
2. **Start `./project-vekta`.** With RPCS3 and the game in their usual places it builds without a question, then
   plays. Otherwise run `scripts/run.sh`: it asks the same questions as `scripts\run.cmd` above, builds, and at
   the end offers an applications-menu entry.
3. **Start `./project-vekta`** to play (`scripts/play.sh` does the same).

## Installing through a launcher

<p>
<a href="https://recomplauncher.com/game/project-vekta"><img src="../assets/press/recomplauncher/badge-dark.svg" width="313" height="56" alt="Get it on RecompLauncher"></a>
<a href="https://quiverlauncher.com/apps/killzone-hd-project-vekta"><img src="../assets/press/quiver/badge@3x.png" width="203" height="56" alt="Get it on Quiver Launcher"></a>
</p>

A game launcher that installs downloaded packages can install the Windows zip or the Linux folder. Each folder has
one program at its top for the launcher to start: `Project Vekta.exe` on Windows, `project-vekta` on Linux.

1. **Add the package** to the launcher (the zip or the `.tar.xz` of a release).
2. **Give it your game folder** when it asks: the installed NPUA80856 folder, with `PARAM.SFO` and `USRDIR`. A
   decrypted `EBOOT.elf` is optional: leave it out when your RPCS3 has the game, and it makes the copy itself.
3. **Tick NVIDIA DLSS** (Windows only) only if you accept NVIDIA's licence; without it the port builds with AMD
   FSR 3.1. The Linux folder always builds with FSR 3.1: NVIDIA's Linux library does not link with its bundled
   toolchain. For DLSS on Linux, build with your system's compilers (docs/BUILDING.md).
4. **Let it prepare.** The launcher runs the check and the build (5 to 30 minutes) without a window, with a
   progress bar. Nothing of the game is downloaded.
5. **Play** from the launcher. The saves and settings stay in the folder (`work/vfs`), and the logs in `work`.

When RPCS3 has the game in its usual places, a launcher that only starts the program also works: the first start
builds from your RPCS3 without asking anything. If a step fails, the launcher shows the reason;
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) says what each one means. For launcher developers:
[dev/launchers.md](dev/launchers.md). RecompLauncher and Quiver Launcher are third-party projects; their names and
the RecompLauncher badge are their owners' (the badges: [assets/press/](../assets/press/)).

## If something goes wrong

- [TROUBLESHOOTING.md](TROUBLESHOOTING.md): what you see, what to do and what to attach, for each exit code.
- [KNOWN_ISSUES.md](KNOWN_ISSUES.md) lists the open problems and their workarounds.
- An install over a version before 0.4.0 that failed at the link: delete `work\build-win` in the install folder
  (Linux: `work/build`) and run `scripts\run.cmd` (`scripts/run.sh`) again. Since 0.4.0 the build empties an old build folder
  itself.
- Open an issue with the bug template and attach the support package or the log from `work\logs\`. Never attach
  game files or saves.
