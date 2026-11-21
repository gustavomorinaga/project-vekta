@echo off
rem Project Vekta, Windows release folder (scripts\play.cmd): plays the port built by run.cmd (tools\release\launch.py),
rem as "Project Vekta.exe" does, in a console.
rem --fullscreen for full screen; the settings are in work\vfs\PS3_GAME\USRDIR\kz1hd_pc.ini, the log in work\play.log.
setlocal
cd /d "%~dp0.."
set "PY=%~dp0..\work\toolchain-cache\python\python.exe"
if not exist "%PY%" (
  echo The bundled Python is missing: %PY%
  echo Unpack or install the whole release again, keeping its folders.
  pause
  exit /b 2
)
"%PY%" "%~dp0..\tools\release\launch.py" --root "%~dp0.." %*
if errorlevel 1 pause
