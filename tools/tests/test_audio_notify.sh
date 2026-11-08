#!/usr/bin/env bash
# Builds and runs test_audio_notify.c against the patched tree (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/audio/cellAudio_notify.h" ]] || { echo "FAIL: no cellAudio_notify.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/audio" "$ROOT/tools/tests/test_audio_notify.c" -o "$out/t"
timeout 60 "$out/t"
