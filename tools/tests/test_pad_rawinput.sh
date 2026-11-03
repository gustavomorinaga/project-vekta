#!/usr/bin/env bash
# Builds and runs test_pad_rawinput.c against the patched tree (tools/ps3recomp-patched.sh first), once with the
# Windows hint choice and once without. Needs SDL2's headers and library (CI installs libsdl2-dev).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/input/cellPad.c" ]] || { echo "FAIL: no patched tree at $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
command -v sdl2-config >/dev/null || { echo "FAIL: sdl2-config not found (install SDL2's development files)" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
# shellcheck disable=SC2046  # sdl2-config prints several words on purpose
for win in 1 0; do
  "${CC:-cc}" -std=gnu17 -O0 -UNDEBUG -w -DPS3RECOMP_PAD_USE_SDL2 -DPAD_SDL_WIN32_HINTS=$win \
    -I "$P/include" -I "$P/libs/input" $(sdl2-config --cflags) \
    "$ROOT/tools/tests/test_pad_rawinput.c" $(sdl2-config --libs) -pthread -lm -o "$out/t$win"
  timeout 60 "$out/t$win"
done
