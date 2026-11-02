; Project Vekta: the Windows setup program (Inno Setup 6). After Project Pyrrhus's installer/kz2.iss and Project
; Helghast's installer/kz3.iss (undecuple, MIT): their pages for the player's own files (checked before the copy), the
; build inside the wizard with a progress bar, a Stop button and the reasons when it stops (tools/release/firstrun.py
; --status-file / --stop-file), and the silent-install switches. The checks here are written for Killzone HD's files.
;
;   iscc /DPayload=<payload folder> installer\vekta.iss     (output: installer\Output\project-vekta-<version>-setup.exe)
;
; The payload is tools/release/assemble_payload.py --launcher ...'s folder: the port's sources, the patched ps3recomp
; and the pinned Windows toolchain, nothing of the game. What setup does: a per-user install (no admin) into
; %LOCALAPPDATA%\Programs\ProjectVekta by default (a folder without spaces: the compiler and CMake take any, but a
; short plain path keeps the tools' command lines readable). It asks where your own files are (docs/GETTING-FILES.md):
; the installed game folder (NPUA80856), the decrypted EBOOT.elf, data_ps3.psarc and libsre.prx, and optionally the
; firmware's dev_flash; it checks their kind and size before copying anything, keeps the answers in work\paths.env
; (firstrun.py --save-paths) and, with "Build now" (the default), builds the port in the wizard (about five minutes;
; the full SHA-256 check is the build's first step). Start-menu entries (the game, "Verify and rebuild", "Open saves
; folder", "Make a support package", the project page, the uninstaller) and an optional desktop shortcut, all on
; the launcher "Project Vekta.exe", which builds on its first start when the build did not run here. It downloads
; nothing.
; Silent install: setup /VERYSILENT /SUPPRESSMSGBOXES [/DIR=<folder>] [/GAME=<dir> /EBOOT=<file> /PSARC=<file>
; /LIBSRE=<file> [/DEVFLASH=<dir>]] [/MERGETASKS="!buildnow"] /LOG=<file>. The pages are skipped; with all four
; files given it saves them and builds (the log says how it went), without them it builds nothing and the first start
; asks. The saves and settings go to %APPDATA%\Project Vekta, or with the "portable" task (/MERGETASKS="portable")
; to the install folder's work\vfs; vekta-install.ini [install] portable tells launch.py which. The uninstaller removes
; what the build made (the lift, the build, the logs, the copies of your game files in the game folder) and keeps your
; saves and settings in both places.

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
#if VER < EncodeVer(6,6,0)
  #error Inno Setup 6.6 or later is needed (WizardStyle dark; tested with 6.7.3)
#endif
#define AppName "Project Vekta"
; The tested files' sizes, as tools/check-game-files.py has them (tools/tests/test_setup.py checks they agree)
#define EbootSize "19706080"
#define PsarcSize "3147174243"
#define LibsreSize "239344"
#define TitleId "NPUA80856"
#define FilesDoc "https://github.com/gustavomorinaga/project-vekta/blob/main/docs/GETTING-FILES.md"
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
; The look: installer\art\ (tools/release/installer_art.py, from the project's own art in the site's palette).
SetupIconFile=art\vekta-setup.ico
UninstallDisplayIcon={app}\vekta-uninstall.ico
UninstallDisplayName={#AppName}
; Dark always, as the site and the game (Inno 6.7.3's help, WizardStyle: pages, message boxes and the title bar;
; a Windows high-contrast theme, or /NOSTYLE, still wins). After Project Pyrrhus's and Project Helghast's setups.
WizardStyle=modern dark
; the Welcome page carries the side image (Inno 6 hides it by default)
DisableWelcomePage=no
; Inno picks the closest size for the screen's DPI (100/125/150/200 %); paths relative to this script
WizardImageFile=art\wizard-100.bmp,art\wizard-125.bmp,art\wizard-150.bmp,art\wizard-200.bmp
WizardSmallImageFile=art\header-100.bmp,art\header-125.bmp,art\header-150.bmp,art\header-200.bmp
WizardImageStretch=yes
Compression=lzma2/max
SolidCompression=yes
OutputDir=Output
OutputBaseFilename=project-vekta-{#AppVersion}-setup
; the build adds about 1.6 GB (the lift, the libraries, the exe), and the game folder takes copies of the EBOOT and the
; archive where links cannot be made (about 4 GB)
ExtraDiskSpaceRequired=5800000000

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"
Name: "buildnow"; Description: "&Build now, in this wizard (about five minutes; else the first start builds)"
Name: "portable"; Description: "&Portable: saves and settings in the install folder (else in %APPDATA%)"; Flags: unchecked

[INI]
; launch.py reads it (install_settings): where the game runs from, so where its saves and settings go.
Filename: "{app}\vekta-install.ini"; Section: "install"; Key: "portable"; String: "1"; Tasks: portable
Filename: "{app}\vekta-install.ini"; Section: "install"; Key: "portable"; String: "0"; Tasks: not portable

[InstallDelete]
; Install over an earlier version: the payload's own folders are replaced whole, so a file a newer version dropped
; cannot linger and be compiled or run, and a toolchain entry of another version leaves nothing behind. Inno keeps
; each file's time stamp, so ninja recompiles only the sources that changed. Never listed here: work\lift, work\fw,
; work\build-win, work\win-deps (firstrun.py's markers decide what runs again), work\vfs (the game folder, and the
; saves of a portable install), work\logs, work\paths.env, vekta-install.ini. After Project Pyrrhus's (MIT).
Type: filesandordirs; Name: "{app}\port"
Type: filesandordirs; Name: "{app}\re"
Type: filesandordirs; Name: "{app}\tools"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\docs"
Type: filesandordirs; Name: "{app}\work\ps3recomp-patched"
Type: filesandordirs; Name: "{app}\work\toolchain-cache"
; v0.2.0's Start menu entry, replaced by "Verify and rebuild"
Type: files; Name: "{group}\Build again (your files, the lift, the compile).lnk"

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Named again: a wildcard skips hidden files, and Wine shows a name that starts with a dot as hidden (firstrun.py
; finds the shipped patched tree by this file). tools/release/build_setup.py also turns Wine's ShowDotFiles on.
Source: "{#Payload}\work\ps3recomp-patched\.patchset"; DestDir: "{app}\work\ps3recomp-patched"; Flags: ignoreversion
; Apps & features' icon (UninstallDisplayIcon) and the Start menu's Uninstall entry
Source: "art\vekta-uninstall.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"
; launch.py --repair: the install against manifest.json, then the check of your files and a rebuild of what changed
Name: "{group}\Verify and rebuild"; Filename: "{app}\{#LauncherExe}"; Parameters: "--repair"; WorkingDir: "{app}"
; the saves and settings folder in Explorer (%APPDATA%\Project Vekta, or work\vfs when portable)
Name: "{group}\Open saves folder"; Filename: "{app}\{#LauncherExe}"; Parameters: "--open-data"; WorkingDir: "{app}"
; one zip of the logs and settings for a bug report, on the Desktop (tools/release/support.py)
Name: "{group}\Make a support package"; Filename: "{app}\{#LauncherExe}"; Parameters: "--support-package"; WorkingDir: "{app}"
Name: "{group}\Project page"; Filename: "{#SiteURL}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"; IconFilename: "{app}\vekta-uninstall.ico"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; Description: "&Play {#AppName} now"; Flags: postinstall nowait skipifsilent; Check: BuildSucceeded
Filename: "{app}\run.cmd"; WorkingDir: "{app}"; Description: "&Build now in a console (it asks again where your files are)"; Flags: postinstall shellexec skipifsilent unchecked; Check: BuildNotDone

[UninstallDelete]
Type: files; Name: "{app}\vekta-install.ini"
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

[Code]
var
  GamePage: TInputDirWizardPage;
  FilesPage: TInputFileWizardPage;
  FlashPage: TInputQueryWizardPage;
  FlashBrowse, StopBtn: TNewButton;
  StopFile: String;
  BuildRan, BuildOK, BuildStopped: Boolean;
  BuildReason, BuildLog: String;

function BuildSucceeded: Boolean;
begin
  Result := BuildRan and BuildOK;
end;

function BuildNotDone: Boolean;
begin
  Result := not BuildSucceeded;
end;

{ One "key=value" field of a firstrun.py PROGRESS / RESULT line }
function Field(Line, Key: String): String;
var
  P: Integer;
begin
  Result := '';
  P := Pos(' ' + Key + '=', ' ' + Line);
  if P = 0 then Exit;
  Result := Copy(Line, P + Length(Key) + 1, MaxInt);
  P := Pos(' ', Result);
  if P > 0 then Result := Copy(Result, 1, P - 1);
end;

{ PARAM.SFO: magic NUL P S F; key table at 8, data table at 12, entry count at 16, 16-byte entries from 20 (key
  offset u16, format u16, length u32, max length u32, data offset u32), little-endian. The layout as Project
  Helghast's installer reads it. '' when the file or the key is not there. }
function SfoInt(S: AnsiString; P, Bytes: Integer): Integer;
var
  B: Integer;
begin
  Result := 0;
  if (P < 0) or (P + Bytes > Length(S)) then Exit;
  for B := Bytes - 1 downto 0 do Result := Result * 256 + Ord(S[P + B + 1]);
end;

function SfoText(S: AnsiString; P, Max: Integer): String;
var
  I: Integer;
begin
  Result := '';
  I := P + 1;
  while (I <= Length(S)) and (I <= P + Max) and (Ord(S[I]) <> 0) do begin
    Result := Result + Chr(Ord(S[I]));
    I := I + 1;
  end;
end;

function SfoField(Path, Key: String): String;
var
  S: AnsiString;
  KT, DT, N, I, E: Integer;
begin
  Result := '';
  if not LoadStringFromFile(Path, S) then Exit;
  if (Length(S) < 20) or (Ord(S[1]) <> 0) or (Ord(S[2]) <> 80) or (Ord(S[3]) <> 83) or (Ord(S[4]) <> 70) then Exit;
  KT := SfoInt(S, 8, 4);
  DT := SfoInt(S, 12, 4);
  N := SfoInt(S, 16, 4);
  if N > 200 then N := 200;
  for I := 0 to N - 1 do begin
    E := 20 + 16 * I;
    if SfoText(S, KT + SfoInt(S, E, 2), 32) = Key then begin
      Result := SfoText(S, DT + SfoInt(S, E + 12, 4), SfoInt(S, E + 4, 4));
      Exit;
    end;
  end;
end;

{ The first four bytes of a file, '' when it cannot be read (a 3 GB archive is not read whole) }
function Magic(Path: String): AnsiString;
var
  F: TFileStream;
  Buf: AnsiString;
begin
  Result := '';
  try
    F := TFileStream.Create(Path, fmOpenRead or fmShareDenyNone);
    try
      { No F.Size test: Pascal Script gives a size over 2 GB as a negative number. A short file raises here. }
      SetLength(Buf, 4);
      F.ReadBuffer(Buf, 4);
      Result := Buf;
    finally
      F.Free;
    end;
  except
    Result := '';
  end;
end;

{ The folder that holds PARAM.SFO, whichever level was picked: NPUA80856 itself, or the folder it is in }
function ResolveGame(Dir: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Dir);
  if (not FileExists(AddBackslash(Result) + 'PARAM.SFO')) and
     FileExists(AddBackslash(Result) + '{#TitleId}\PARAM.SFO') then
    Result := AddBackslash(Result) + '{#TitleId}';
end;

function GameProblem(Dir: String): String;
var
  Id: String;
begin
  Result := '';
  if not FileExists(AddBackslash(Dir) + 'PARAM.SFO') then begin
    Result := 'No PARAM.SFO in this folder. Pick the installed game folder: {#TitleId}, installed from the ' +
      'Killzone Trilogy disc''s Killzone HD package (it holds PARAM.SFO, TROPDIR and USRDIR; in RPCS3 it is ' +
      'dev_hdd0\game\{#TitleId}).';
    Exit;
  end;
  Id := SfoField(AddBackslash(Dir) + 'PARAM.SFO', 'TITLE_ID');
  if Id <> '{#TitleId}' then
    Result := 'This is not Killzone HD: its PARAM.SFO says title ' + Id + ', the port needs {#TitleId} ' +
      '(the Killzone HD package of the Killzone Trilogy disc).';
end;

{ '' when Path looks like the tested file (its kind by the first bytes, its size), else what is wrong. The build's
  first step (tools/check-game-files.py) then checks the SHA-256. }
function FileProblem(Path, Name, Kind: String; Size: Int64): String;
var
  Got: Int64;
  M: AnsiString;
begin
  Result := '';
  if Path = '' then begin
    Result := Name + ': no file picked.';
    Exit;
  end;
  if not FileExists(Path) then begin
    Result := Name + ': there is no file at ' + Path + '.';
    Exit;
  end;
  M := Magic(Path);
  if not (((Kind = 'ELF') and (M = #127 + 'ELF')) or ((Kind = 'PSAR') and (M = 'PSAR'))) then begin
    if M = 'SCE' + #0 then
      Result := Name + ': this is the encrypted container (as EBOOT.BIN or a .sprx), not the decrypted file.'
    else if (Kind = 'PSAR') and (CompareText(ExtractFileExt(Path), '.edat') = 0) then
      Result := Name + ': this is the .edat the package installs, not the decrypted archive.'
    else
      Result := Name + ': this is not the right kind of file (it should start as ' + Kind + ').';
    Exit;
  end;
  if FileSize64(Path, Got) and (Got <> Size) then
    Result := Name + ': it is ' + IntToStr(Got) + ' bytes; the tested one is ' + IntToStr(Size) + ' bytes, so it is ' +
      'another version, which will not work.';
end;

function FilesProblem: String;
var
  P: String;
begin
  Result := '';
  P := FileProblem(FilesPage.Values[0], 'EBOOT.elf', 'ELF', StrToInt64('{#EbootSize}'));
  if P <> '' then Result := Result + P + #13#10;
  P := FileProblem(FilesPage.Values[1], 'data_ps3.psarc', 'PSAR', StrToInt64('{#PsarcSize}'));
  if P <> '' then Result := Result + P + #13#10;
  P := FileProblem(FilesPage.Values[2], 'libsre.prx', 'ELF', StrToInt64('{#LibsreSize}'));
  if P <> '' then Result := Result + P + #13#10;
end;

function FlashOK(Dir: String): Boolean;
begin
  Result := FileExists(AddBackslash(Dir) + 'sys\external\flashMP3.pic');
end;

{ An empty file field gets the file of that name next to one already picked, or in the game's USRDIR }
procedure FillFile(I: Integer; Name: String);
var
  J: Integer;
  D: String;
begin
  if FilesPage.Values[I] <> '' then Exit;
  for J := 0 to 2 do
    if (J <> I) and (FilesPage.Values[J] <> '') then begin
      D := AddBackslash(ExtractFileDir(FilesPage.Values[J])) + Name;
      if FileExists(D) then begin
        FilesPage.Values[I] := D;
        Exit;
      end;
    end;
  D := AddBackslash(GamePage.Values[0]) + 'USRDIR\' + Name;
  if FileExists(D) then FilesPage.Values[I] := D;
end;

procedure FillFiles;
begin
  FillFile(0, 'EBOOT.elf');
  FillFile(1, 'data_ps3.psarc');
  FillFile(2, 'libsre.prx');
end;

procedure FlashBrowseClick(Sender: TObject);
var
  D: String;
begin
  D := FlashPage.Values[0];
  if BrowseForFolder('The firmware''s dev_flash folder (holds sys\external\flashMP3.pic):', D, False) then
    FlashPage.Values[0] := D;
end;

{ A value from the command line (/GAME= and the others), else the last install's }
function Pick(Param, Prev: String): String;
begin
  Result := ExpandConstant('{param:' + Param + '|}');
  if Result = '' then Result := GetPreviousData(Prev, '');
end;

procedure InitializeWizard;
begin
  GamePage := CreateInputDirPage(wpSelectTasks, 'Your game',
    'The installed Killzone HD ({#TitleId}), from your own Killzone Trilogy disc.',
    'Pick the installed game folder: it holds PARAM.SFO, TROPDIR and USRDIR (in RPCS3: dev_hdd0\game\{#TitleId}; ' +
    'picking the folder it is in also works). Setup only reads it.', False, '');
  GamePage.Add('Installed game folder ({#TitleId}):');
  FilesPage := CreateInputFilePage(GamePage.ID, 'Your decrypted files',
    'Three files made from your own game and PS3 firmware.',
    'Decrypted, as docs/GETTING-FILES.md says: not EBOOT.BIN, not the .edat archive, not the .sprx. This installer ' +
    'includes no decryption tools and downloads nothing; the files are only read.');
  FilesPage.Add('EBOOT.elf (the game''s executable, decrypted):', 'ELF files (*.elf)|*.elf|All files|*.*', '.elf');
  FilesPage.Add('data_ps3.psarc (the data archive, decrypted; about 3 GB):', 'PSARC files (*.psarc)|*.psarc|All files|*.*', '.psarc');
  FilesPage.Add('libsre.prx (SPURS, from PS3 system software 4.93, decrypted):', 'PRX files (*.prx)|*.prx|All files|*.*', '.prx');
  FlashPage := CreateInputQueryPage(FilesPage.ID, 'PS3 firmware (for the music)',
    'The firmware''s dev_flash folder, as RPCS3 keeps it.',
    'The game''s music decoder is read from dev_flash\sys\external\flashMP3.pic. Optional, but without it the music ' +
    'and sound stop at the first MP3. RPCS3 keeps dev_flash in its own folder.');
  FlashPage.Add('dev_flash folder (holds sys\external\flashMP3.pic):', False);
  FlashPage.Edits[0].Width := FlashPage.SurfaceWidth - ScaleX(90);
  FlashBrowse := TNewButton.Create(FlashPage);
  FlashBrowse.Parent := FlashPage.Surface;
  FlashBrowse.Left := FlashPage.SurfaceWidth - ScaleX(80);
  FlashBrowse.Top := FlashPage.Edits[0].Top - ScaleY(1);
  FlashBrowse.Width := ScaleX(80);
  FlashBrowse.Height := FlashPage.Edits[0].Height + ScaleY(2);
  FlashBrowse.Caption := 'B&rowse...';
  FlashBrowse.OnClick := @FlashBrowseClick;
  GamePage.Values[0] := Pick('GAME', 'Game');
  FilesPage.Values[0] := Pick('EBOOT', 'Eboot');
  FilesPage.Values[1] := Pick('PSARC', 'Psarc');
  FilesPage.Values[2] := Pick('LIBSRE', 'Libsre');
  FlashPage.Values[0] := Pick('DEVFLASH', 'DevFlash');
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Game', GamePage.Values[0]);
  SetPreviousData(PreviousDataKey, 'Eboot', FilesPage.Values[0]);
  SetPreviousData(PreviousDataKey, 'Psarc', FilesPage.Values[1]);
  SetPreviousData(PreviousDataKey, 'Libsre', FilesPage.Values[2]);
  SetPreviousData(PreviousDataKey, 'DevFlash', FlashPage.Values[0]);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = FilesPage.ID then FillFiles;
end;

{ The install folder (after Project Helghast's and Project Pyrrhus's checks, MIT): a character CMake cannot take in
  a path ('[' and ']' its list brackets, ';' its list separator: the build would stop while configuring), OneDrive
  (it syncs and locks the build's thousands of files while they are written), or a folder of the system or the
  user's own (a drive's root, Windows, Program Files, Documents, the Desktop, the profile, Downloads). }
function CMakeBadChar(Dir: String): String;
begin
  Result := '';
  if Pos('[', Dir) > 0 then Result := '['
  else if Pos(']', Dir) > 0 then Result := ']'
  else if Pos(';', Dir) > 0 then Result := ';';
end;

function InOneDrive(Dir: String): Boolean;
var
  D, B: String;
  I: Integer;
  Vars: array of String;
begin
  Result := False;
  D := Lowercase(RemoveBackslashUnlessRoot(Dir));
  SetArrayLength(Vars, 3);
  Vars[0] := 'OneDrive';
  Vars[1] := 'OneDriveConsumer';
  Vars[2] := 'OneDriveCommercial';
  for I := 0 to 2 do begin
    B := Lowercase(RemoveBackslashUnlessRoot(GetEnv(Vars[I])));
    if (B <> '') and ((D = B) or (Pos(B + '\', D) = 1)) then Result := True;
  end;
end;

function SameDir(D, Other: String): Boolean;
begin
  Other := RemoveBackslashUnlessRoot(Other);
  Result := (Other <> '') and (D = Lowercase(Other));
end;

function IsProtectedFolder(Dir: String): Boolean;
var
  D, Prof: String;
begin
  D := Lowercase(RemoveBackslashUnlessRoot(Dir));
  Prof := GetEnv('USERPROFILE');
  Result := SameDir(D, ExtractFileDrive(Dir)) or SameDir(D, ExpandConstant('{win}')) or
    SameDir(D, ExpandConstant('{commonpf}')) or SameDir(D, ExpandConstant('{commonpf32}')) or
    SameDir(D, ExpandConstant('{userdocs}')) or SameDir(D, ExpandConstant('{userdesktop}')) or
    SameDir(D, Prof) or ((Prof <> '') and SameDir(D, AddBackslash(Prof) + 'Downloads'));
end;

{ '' when Dir is a good install folder, else why not }
function InstallDirProblem(Dir: String): String;
var
  D, Fine: String;
begin
  Result := '';
  D := RemoveBackslashUnlessRoot(Dir);
  Fine := ' (the default, ' + ExpandConstant('{localappdata}') + '\Programs\ProjectVekta, is fine).';
  if CMakeBadChar(D) <> '' then
    Result := 'The folder ' + D + ' has the character "' + CMakeBadChar(D) + '" in its path. The build tools ' +
      '(CMake) cannot use a folder with [ ] or ; and would stop. Choose a folder without them' + Fine
  else if InOneDrive(D) then
    Result := 'The folder ' + D + ' is inside OneDrive, which syncs the build''s thousands of files while they are ' +
      'written and locks them. Choose a folder outside OneDrive' + Fine
  else if IsProtectedFolder(D) then
    Result := 'This folder cannot be the install folder: ' + D + '. Choose a folder of its own' + Fine;
end;

{ A silent install never presses Next on the folder page: a bad /DIR= stops here (exit code 7), before any file }
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := InstallDirProblem(ExpandConstant('{app}'));
  if Result <> '' then Log('install folder refused: ' + Result);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem: String;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and not WizardSilent then begin
    Problem := InstallDirProblem(WizardDirValue);
    if Problem <> '' then begin
      MsgBox(Problem, mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = GamePage.ID then begin
    GamePage.Values[0] := ResolveGame(GamePage.Values[0]);
    Problem := GameProblem(GamePage.Values[0]);
    if Problem <> '' then begin
      MsgBox(Problem, mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = FilesPage.ID then begin
    Problem := FilesProblem;
    if Problem <> '' then begin
      MsgBox(Problem + #13#10 + 'What each file must be: {#FilesDoc}', mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = FlashPage.ID then begin
    FlashPage.Values[0] := RemoveBackslashUnlessRoot(Trim(FlashPage.Values[0]));
    if FlashPage.Values[0] = '' then
      Result := MsgBox('Without dev_flash the music and sound stop at the first MP3. Go on without it?',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES
    else if not FlashOK(FlashPage.Values[0]) then begin
      MsgBox('No sys\external\flashMP3.pic in this folder. Pick the firmware''s dev_flash folder, or leave the ' +
        'field empty to go on without the music.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

{ Silent installs never press Next: the pages are skipped, and the values are checked here instead }
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := WizardSilent and ((PageID = GamePage.ID) or (PageID = FilesPage.ID) or (PageID = FlashPage.ID));
end;

{ All four required files, checked; silent installs with any of them missing build nothing }
function FilesReady: Boolean;
var
  P: String;
begin
  P := '';
  if GamePage.Values[0] = '' then P := 'no game folder. '
  else P := GameProblem(ResolveGame(GamePage.Values[0]));
  P := P + FilesProblem;
  Result := P = '';
  if not Result then Log('game files: ' + P);
end;

function Quote(S: String): String;
begin
  Result := '"' + S + '"';
end;

function PathArgs: String;
begin
  Result := '';
  if GamePage.Values[0] <> '' then Result := Result + ' --game ' + Quote(ResolveGame(GamePage.Values[0]));
  if FilesPage.Values[0] <> '' then Result := Result + ' --eboot ' + Quote(FilesPage.Values[0]);
  if FilesPage.Values[1] <> '' then Result := Result + ' --psarc ' + Quote(FilesPage.Values[1]);
  if FilesPage.Values[2] <> '' then Result := Result + ' --libsre ' + Quote(FilesPage.Values[2]);
  if FlashPage.Values[0] <> '' then Result := Result + ' --dev-flash ' + Quote(FlashPage.Values[0]);
end;

function StepLabel(Name: String): String;
begin
  { The steps of tools/release/firstrun.py (tools/tests/test_setup.py checks the names) }
  if Name = 'check' then Result := 'Checking your game files'
  else if Name = 'venv' then Result := 'Installing the build tools'
  else if Name = 'patch' then Result := 'Patching ps3recomp'
  else if Name = 'lift' then Result := 'Translating the game''s code'
  else if Name = 'vfs' then Result := 'Preparing the game folder'
  else if Name = 'compile' then Result := 'Compiling'
  else Result := 'Finishing';
end;

function Clock(Secs: Integer): String;
begin
  Result := Format('%d:%.2d', [Secs div 60, Secs mod 60]);
end;

procedure StopClick(Sender: TObject);
begin
  { firstrun.py --stop-file: it ends the running tool, keeps the steps already done and writes RESULT
    step=cancelled; the next start resumes there. }
  if MsgBox('Stop the build now?' + #13#10 + #13#10 + 'The steps already done are kept: the next start of ' +
            '{#AppName} resumes where it stopped.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then Exit;
  SaveStringToFile(StopFile, 'stop', False);
  Log('build: stop requested');
  StopBtn.Enabled := False;
  StopBtn.Caption := 'Stopping...';
end;

procedure ShowBuildError;
var
  RC: Integer;
begin
  if MsgBox('The build did not finish:' + #13#10 + #13#10 + BuildReason + #13#10 + #13#10 +
            'Its log: ' + BuildLog + #13#10 + #13#10 + 'Open the log now? (Start {#AppName} after a fix: the build ' +
            'resumes where it stopped.)', mbError, MB_YESNO) = IDYES then
    ShellExec('open', BuildLog, '', '', SW_SHOW, ewNoWait, RC);
end;

procedure RunBuild;
var
  Page: TOutputProgressWizardPage;
  Py, Status, Params, L, Name, Pct: String;
  Lines: TArrayOfString;
  RC, P, I: Integer;
  Finished: Boolean;
begin
  Py := ExpandConstant('{app}\work\toolchain-cache\python\python.exe');
  Status := ExpandConstant('{tmp}\firstrun.status');
  StopFile := ExpandConstant('{tmp}\firstrun.stop');
  Params := Quote(ExpandConstant('{app}\tools\release\firstrun.py')) + ' --root ' + Quote(ExpandConstant('{app}')) +
    PathArgs + ' --status-file ' + Quote(Status) + ' --stop-file ' + Quote(StopFile);
  Log('firstrun: ' + Py + ' ' + Params);
  BuildRan := True;
  Page := CreateOutputProgressPage('Building {#AppName}', 'From your own game files: about five minutes.');
  StopBtn := TNewButton.Create(Page);
  StopBtn.Parent := Page.Surface;
  StopBtn.Left := Page.ProgressBar.Left;
  StopBtn.Top := Page.ProgressBar.Top + Page.ProgressBar.Height + ScaleY(16);
  StopBtn.Width := ScaleX(110);
  StopBtn.Height := ScaleY(24);
  StopBtn.Caption := 'Stop the build';
  StopBtn.OnClick := @StopClick;
  Page.SetProgress(0, 1000);
  Page.Show;
  try
    if not Exec(Py, Params, ExpandConstant('{app}'), SW_HIDE, ewNoWait, RC) then begin
      BuildReason := 'Could not start the bundled Python (' + SysErrorMessage(RC) + ').';
      Log('build: ' + BuildReason);
      if not WizardSilent then MsgBox(BuildReason, mbError, MB_OK);
      Exit;
    end;
    Finished := False;
    { SetProgress lets the window answer clicks (the Stop button) and repaint; Sleep alone would not }
    while not Finished do begin
      Sleep(250);
      Page.SetProgress(Page.ProgressBar.Position, 1000);
      if LoadStringsFromFile(Status, Lines) and (GetArrayLength(Lines) >= 2) then begin
        if Pos('LOG ', Lines[0]) = 1 then BuildLog := Copy(Lines[0], 5, MaxInt);
        L := Lines[1];
        if Pos('RESULT ', L) = 1 then begin
          Finished := True;
          BuildOK := Field(L, 'ok') = '1';
          BuildStopped := Field(L, 'step') = 'cancelled';
          BuildReason := '';
          for I := 2 to GetArrayLength(Lines) - 1 do
            if Pos('REASON ', Lines[I]) = 1 then BuildReason := BuildReason + Copy(Lines[I], 8, MaxInt) + #13#10;
        end else if Pos('PROGRESS ', L) = 1 then begin
          Name := Field(L, 'name');
          Pct := Field(L, 'pct');
          P := Pos('.', Pct);
          if P > 0 then Pct := Copy(Pct, 1, P - 1);
          if Name = 'compile' then
            Page.SetText(StepLabel(Name) + ' ' + Field(L, 'done') + ' / ' + Field(L, 'total'),
              'Step ' + Field(L, 'step') + ', ' + Clock(StrToIntDef(Field(L, 'elapsed'), 0)))
          else
            Page.SetText(StepLabel(Name), 'Step ' + Field(L, 'step') + ', ' + Clock(StrToIntDef(Field(L, 'elapsed'), 0)));
          Page.SetProgress(StrToIntDef(Pct, 0) * 10, 1000);
        end;
      end;
    end;
    StopBtn.Visible := False;
    if BuildStopped then
      Log('build: stopped by the player; the first start resumes it')
    else if not BuildOK then begin
      Log('build: did not finish: ' + BuildReason + ' (log ' + BuildLog + ')');
      if not WizardSilent then ShowBuildError;
    end else
      Log('build: finished (log ' + BuildLog + ')');
  finally
    Page.Hide;
  end;
end;

procedure SavePaths;
var
  RC: Integer;
begin
  if PathArgs = '' then Exit;
  if Exec(ExpandConstant('{app}\work\toolchain-cache\python\python.exe'),
          Quote(ExpandConstant('{app}\tools\release\firstrun.py')) + ' --root ' + Quote(ExpandConstant('{app}')) +
          ' --save-paths' + PathArgs, ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, RC) and (RC = 0) then
    Log('paths: saved in work\paths.env')
  else
    Log('paths: not saved (' + IntToStr(RC) + ')');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then Exit;
  SavePaths;
  if not WizardIsTaskSelected('buildnow') then Exit;
  if FilesReady then
    RunBuild
  else
    Log('build: not run, the game files are not all given or not right (silent install); the first start asks');
end;

{ Uninstall: the saves and settings stay unless the player says otherwise (default No; a silent uninstall keeps
  them). They are in %APPDATA%\Project Vekta (launch.py SAVE_FOLDER), or in work\vfs in a portable install, where
  only what the game wrote goes: [UninstallDelete] has already taken the copies and links of the game files. }
function AskDelete(Where: String): Boolean;
begin
  Result := MsgBox('Delete your Project Vekta saves and settings too?' + #13#10 + Where + #13#10 + #13#10 +
    'No keeps them for a later install.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data, Vfs: String;
begin
  if (CurUninstallStep <> usPostUninstall) or UninstallSilent then Exit;
  Data := ExpandConstant('{userappdata}\Project Vekta');
  if DirExists(Data) and AskDelete(Data) then DelTree(Data, True, True, True);
  Vfs := ExpandConstant('{app}\work\vfs');
  if (DirExists(Vfs + '\gamedata') or FileExists(Vfs + '\kz1hd_pc.ini') or FileExists(Vfs + '\kz1hd_keys.ini')) and
     AskDelete(Vfs) then begin
    DelTree(Vfs + '\gamedata', True, True, True);
    DeleteFile(Vfs + '\kz1hd_pc.ini');
    DeleteFile(Vfs + '\kz1hd_keys.ini');
    RemoveDir(Vfs + '\PS3_GAME\USRDIR');
    RemoveDir(Vfs + '\PS3_GAME');
    RemoveDir(Vfs);
    RemoveDir(ExpandConstant('{app}\work'));
    RemoveDir(ExpandConstant('{app}'));
  end;
end;
