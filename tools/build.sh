#!/bin/sh
# Linux build of the port: work/build/KZ1HD from the lift (tools/lift.sh) and the patched ps3recomp, with clang,
# Ninja and the Vulkan renderer. KZ_MARCH sets -march (default x86-64-v3; x86-64-v4 for AVX-512 machines).
# tools/pgo.sh rebuilds the same directory with a profile afterwards.
# The steps themselves are in tools/kzbuild.py (the same code on Windows); this runs them.
exec python3 "$(dirname "$(readlink -f "$0")")/kzbuild.py" build "$@"
