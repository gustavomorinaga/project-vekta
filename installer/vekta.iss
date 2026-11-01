; Project Vekta: the Windows setup program (Inno Setup 6). After Project Pyrrhus's installer/kz2.iss and Project
; Helghast's installer/kz3.iss (undecuple, MIT), much shorter: no pages of its own; the first run's questions are
; run.cmd's (tools/release/firstrun.py --interactive).
;
;   iscc /DPayload=<payload folder> installer\vekta.iss     (output: installer\Output\project-vekta-<version>-setup.exe)
;
; The payload is tools/release/assemble_payload.py --launcher ...'s folder: the port's sources, the patched ps3recomp
; and the pinned Windows toolchain, nothing of the game. What setup does: a per-user install (no admin) into
; %LOCALAPPDATA%\Programs\ProjectVekta by default (a folder without spaces: the compiler and CMake take any, but a
; short plain path keeps the tools' command lines readable); Start-menu entries (the game, "Build again", the project
; page, the uninstaller) and an optional desktop shortcut, all on the launcher "Project Vekta.exe"; on the finish page,
; "Build now" runs run.cmd, which asks where your own files are, checks them and compiles the port here (about five
; minutes; the launcher does the same on its first start if you skip it). It downloads nothing.
; Silent install: setup /VERYSILENT /SUPPRESSMSGBOXES [/DIR=<folder>] /LOG=<file> (nothing is built; the first
; start asks). The uninstaller removes what the build made (the lift, the build, the logs, the copies of your game
; files in the game folder) and keeps your saves and settings in work\vfs.

#ifndef Payload
  #define Payload "..\work\release\payload"
#endif
#define VerH FileOpen(Payload + "\VERSION")
#if !VerH
  #error cannot read {#Payload}\VERSION (assemble the payload first: tools/release/assemble_payload.py)
#endif
#define PayloadVersion Trim(FileRead(VerH))
#expr FileClose(VerH)
#ifndef AppVersion
  #define AppVersion PayloadVersion
#endif
#if AppVersion != PayloadVersion
  #error AppVersion {#AppVersion} is not the payload's VERSION {#PayloadVersion}
#endif
#if VER < EncodeVer(6,5,0)
  #error Inno Setup 6.5 or later is needed (tested with 6.7.3)
#endif
#define AppName "Project Vekta"
#define LauncherExe "Project Vekta.exe"
#define SiteURL "https://gustavomorinaga.github.io/project-vekta/"
#define IssuesURL "https://github.com/gustavomorinaga/project-vekta/issues"

[Setup]
AppId={{CB817149-6B3B-4EEA-88D9-BACB092686D5}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Project Vekta contributors
AppPublisherURL={#SiteURL}
AppSupportURL={#IssuesURL}
AppUpdatesURL=https://github.com/gustavomorinaga/project-vekta/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\ProjectVekta
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#Payload}\LICENSE
SetupIconFile=art\vekta.ico
UninstallDisplayIcon={app}\{#LauncherExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=Output
OutputBaseFilename=project-vekta-{#AppVersion}-setup
; the build adds about 1.6 GB (the lift, the libraries, the exe), and the game folder takes copies of the EBOOT and the
; archive where links cannot be made (about 4 GB)
ExtraDiskSpaceRequired=5800000000

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Named again: a wildcard skips hidden files, and Wine shows a name that starts with a dot as hidden (firstrun.py
; finds the shipped patched tree by this file). tools/release/build_setup.py also turns Wine's ShowDotFiles on.
Source: "{#Payload}\work\ps3recomp-patched\.patchset"; DestDir: "{app}\work\ps3recomp-patched"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"
Name: "{group}\Build again (your files, the lift, the compile)"; Filename: "{app}\run.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\{#LauncherExe}"
Name: "{group}\Project page"; Filename: "{#SiteURL}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\run.cmd"; WorkingDir: "{app}"; Description: "&Build now: asks where your own game files are, then compiles the port (about five minutes)"; Flags: postinstall shellexec skipifsilent

[UninstallDelete]
; What the build made (tools/release/firstrun.py, tools/kzbuild.py); work\vfs keeps your saves and settings.
Type: filesandordirs; Name: "{app}\work\lift"
Type: filesandordirs; Name: "{app}\work\fw"
Type: filesandordirs; Name: "{app}\work\build-win"
Type: filesandordirs; Name: "{app}\work\win-deps"
Type: filesandordirs; Name: "{app}\work\win-deps-build"
Type: filesandordirs; Name: "{app}\work\logs"
Type: filesandordirs; Name: "{app}\work\.firstrun"
Type: files; Name: "{app}\work\win-deps.log"
; zlib's CMake renames its zconf.h in the source tree when build_win_deps.py builds it
Type: files; Name: "{app}\work\toolchain-cache\zlib\zconf.h.included"
Type: files; Name: "{app}\work\build-stamp.json"
Type: files; Name: "{app}\work\paths.env"
Type: files; Name: "{app}\work\play.log"
Type: files; Name: "{app}\launcher-dry-run.txt"
Type: files; Name: "{app}\work\vfs\PS3_GAME\USRDIR\EBOOT.elf"
Type: files; Name: "{app}\work\vfs\PS3_GAME\USRDIR\data_ps3.psarc"
Type: files; Name: "{app}\work\vfs\PS3_GAME\PARAM.SFO"
Type: filesandordirs; Name: "{app}\work\vfs\PS3_GAME\TROPDIR"
Type: filesandordirs; Name: "{app}\work\vfs\PS3_GAME\USRDIR\Movies"
Type: filesandordirs; Name: "{app}\work\vfs\PS3_GAME\USRDIR\Menu"
Type: filesandordirs; Name: "{app}\tools\__pycache__"
Type: filesandordirs; Name: "{app}\tools\release\__pycache__"
Type: filesandordirs; Name: "{app}\work\ps3recomp-patched\tools\__pycache__"
Type: filesandordirs; Name: "{app}\work\toolchain-cache\python\__pycache__"
