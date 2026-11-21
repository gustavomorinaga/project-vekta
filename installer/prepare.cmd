@echo off
rem Project Vekta, Windows release folder (scripts\prepare.cmd): checks your files and builds without a window or a
rem question (tools\release\launch.py --prepare), for a launcher. Prints "progress <0..1> <text>" lines and, on a
rem failure, one "reason <text>" line; exit 0 when built, else a code of port\kz1hd\kz_exit.h. Its options pass through:
rem --game DIR, --eboot FILE, --psarc FILE, --rpcs3 [DIR], --timeout SECS.
setlocal
cd /d "%~dp0.."
set "PY=%~dp0..\work\toolchain-cache\python\python.exe"
if not exist "%PY%" (
  echo reason The bundled Python is missing: unpack or install the whole release again, keeping its folders.
  exit /b 1
)
"%PY%" "%~dp0..\tools\release\launch.py" --root "%~dp0.." --prepare %*
exit /b %ERRORLEVEL%
