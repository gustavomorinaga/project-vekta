#!/bin/sh
# Generates everything the port compiles from your own files (tools/env.sh): the PPU lift of the EBOOT, its SPU
# images, policy module and job modules, the firmware's MP3 decoder and the firmware libsre. Output in $KZ_WORK/lift.
# Run tools/ps3recomp-patched.sh first, and again after changing port/patches (the lifters are patched too).
# The steps themselves are in tools/kzbuild.py (the same code on Windows); this runs them.
exec python3 "$(dirname "$(readlink -f "$0")")/kzbuild.py" lift "$@"
