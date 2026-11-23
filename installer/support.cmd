@echo off
rem Project Vekta, Windows release folder (scripts\support.cmd): makes the support package, one zip of the logs, the
rem crash reports and the settings for a bug report (tools\release\launch.py --support-package, the Start menu's
rem "Make a support package"), on the Desktop; --out DIR puts it elsewhere. Your user folder is written as ~, and it
rem holds no saves and no game files. Nothing is sent anywhere: you attach the zip to your bug report.
setlocal
cd /d "%~dp0.."
set "PY=%~dp0..\work\toolchain-cache\python\python.exe"
if not exist "%PY%" (
  echo The bundled Python is missing: %PY%
  echo Unpack or install the whole release again, keeping its folders.
  pause
  exit /b 2
)
"%PY%" "%~dp0..\tools\release\launch.py" --root "%~dp0.." --support-package %*
pause
