# Installing Project Vekta

This page is for players. It describes version <!-- version -->0.5.0<!-- /version -->: the Windows setup program, the Windows zip,
and the Linux folder. Building from a Git clone is in [BUILDING.md](BUILDING.md).

Every download builds the port on your own PC, from your own game. Nothing of the game is in them, and nothing of the
game is downloaded. The first build takes about five minutes.

## Before you start

You need these files from your own game and your own PS3 firmware.
[GETTING-FILES.md](GETTING-FILES.md) says what each one must be and lists the tested sizes and SHA-256 sums.

- The installed game folder of **Killzone HD** (NPUA80856), from the **Killzone Trilogy** disc. It holds
  `PARAM.SFO`, `TROPDIR` and `USRDIR`. In RPCS3 it is `dev_hdd0\game\NPUA80856`.
- `EBOOT.elf`: the game's executable, decrypted. Not `EBOOT.BIN`.
- Optional: `data_ps3.psarc`, the data archive, decrypted, about 3 GB. Without it the port reads the game folder's
  own `USRDIR\data_ps3.psarc.edat`, once, after the build: about a minute more, and 3.1 GB more disk in `work`.
- `libsre.prx`: from PS3 system software 4.93, decrypted. Not `libsre.sprx`.
- Optional: the firmware's `dev_flash` folder, as RPCS3 keeps it. It holds `sys\external\flashMP3.pic`, the game's
  music decoder. Without it, the music and sound stop at the first MP3.

**Already playing it in RPCS3?** When RPCS3 has Killzone HD and the PS3 firmware 4.93 installed, the Linux folder's
`./run.sh` and the zip's `run.cmd` find them and offer to use them ("Use my RPCS3 install", "I have the files
ready" or "Show me how", which lists these steps): your RPCS3 then decrypts copies of `EBOOT.BIN` and
`libsre.sprx` into the port's own `work\prep` folder, the data archive is read as it is, and you pick no file. The
setup program offers the same on its "Your game" page ("Use my RPCS3 install").

This project gives no decryption tools or instructions. The setup and the scripts only read your files; on the RPCS3
route your own RPCS3 decrypts copies of them, and nothing is written into the game's or RPCS3's folders.

Your PC: Windows x64 or Linux x64, a CPU with AVX2, and a Vulkan graphics card and driver
([requirements.md](requirements.md)). Beside the 4 GB of game files, keep free about 2.4 GB on Windows (3.2 GB
more where Windows cannot make links, so the setup asks for 5.8 GB) or about 1.9 GB on Linux.

Download from the [releases page](https://github.com/gustavomorinaga/project-vekta/releases). Each file has a
`.sha256` beside it, and `SHA256SUMS` lists them all.

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
     `%APPDATA%\Project Vekta`. To change it later, run `play.cmd --move-data portable` (or `--move-data appdata`)
     in the install folder: it moves the saves and settings and remembers the choice, and moves nothing when the
     other folder already has some.
3. **Your game.** "Use my RPCS3 install" when the setup found RPCS3 with the game and the firmware: the next two
   pages are skipped. "Show me how" lists the steps to get there, and stays on the page. Otherwise, pick the
   installed game folder. Picking the folder above it also works.
4. **Your decrypted files.** Pick `EBOOT.elf` and `libsre.prx`. Leave `data_ps3.psarc` empty to use the game
   folder's own `.edat`, or pick a decrypted one you already have.
5. **PS3 firmware (for the music).** Pick the `dev_flash` folder, or leave it empty.
6. **NVIDIA DLSS (optional).** The box is unticked by default. Ticking it accepts NVIDIA's DLSS SDK licence, and
   the build then downloads DLSS from NVIDIA's repository. Left unticked, nothing of NVIDIA's is downloaded, and the
   game uses AMD FSR 3.1 as its temporal upscaler.
7. **Install.** Setup checks each file's kind and size before it copies anything. With "Build now", the wizard then
   builds the port with a progress bar and a Stop button. The build's first step checks the files' SHA-256. When the
   build stops, the wizard says why and opens its log.
8. **Play.** At the end, tick "Play Project Vekta now", or start **Project Vekta** from the Start menu or the desktop.
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
2. **Run `run.cmd`.** A console window asks for each file in turn, with a suggested path in brackets. Type or paste
   a path, or press Enter to keep the one in brackets.
3. **Fix what the check names.** If the check does not pass, the window says which file is wrong and why. Press
   Enter to choose that file again, or `q` to quit.
4. **Answer the DLSS question.** It shows NVIDIA's licence link. `y` accepts it and downloads DLSS; Enter or `n`
   builds without it.
5. **Wait for the build.** A progress bar runs for about five minutes. At the end the window says "Built." and
   names the command that plays. The log is in `work\logs\`.
6. **Run `play.cmd`.** Add `--fullscreen` for full screen. The settings are in
   `work\vfs\PS3_GAME\USRDIR\kz1hd_pc.ini` and the log in `work\play.log`.

Run `run.cmd` again after you move your files. A build that was stopped resumes there.
`run.cmd --dlss accept` or `run.cmd --dlss decline` changes the DLSS answer.

## Linux: the folder

The file is `project-vekta-X.Y.Z-linux.tar.gz`. It has its own compiler and build tools.
You need Python 3.10 or newer, glibc 2.31 or newer, SDL2 and a Vulkan driver.

1. **Unpack it:** `tar xzf project-vekta-X.Y.Z-linux.tar.gz`.
2. **Run `./run.sh`.** It asks the same questions as `run.cmd` above, then builds. At the end it offers an
   applications-menu entry.
3. **Run `./play.sh`** to play.

## If something goes wrong

- [KNOWN_ISSUES.md](KNOWN_ISSUES.md) lists the open problems and their workarounds.
- An install over a version before 0.4.0 that failed at the link: delete `work\build-win` in the install folder
  (Linux: `work/build`) and run `run.cmd` (`run.sh`) again. Since 0.4.0 the build empties an old build folder
  itself.
- Open an issue with the bug template and attach the support package or the log from `work\logs\`. Never attach
  game files or saves.
