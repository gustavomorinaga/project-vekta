@echo off
rem Project Vekta, Windows release folder: the first run. Asks where your own files are (docs\GETTING-FILES.md),
rem checks them, then lifts the game's code and compiles the port here with the bundled toolchain, with one
rem progress bar (tools\release\firstrun.py). Run it again after moving your files; a stopped run resumes.
rem Nothing is downloaded, except NVIDIA DLSS if you accept its licence when asked (run.cmd --dlss accept or
rem --dlss decline changes the answer later). The log is in work\logs\.
setlocal
cd /d "%~dp0"
set "PY=%~dp0work\toolchain-cache\python\python.exe"
if not exist "%PY%" (
  echo The bundled Python is missing: %PY%
  echo Unpack or install the whole release again, keeping its folders.
  pause
  exit /b 2
)
"%PY%" "%~dp0tools\release\firstrun.py" --interactive --root "%~dp0." %*
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
