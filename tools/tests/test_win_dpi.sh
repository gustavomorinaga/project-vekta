#!/usr/bin/env bash
# Builds and runs test_win_dpi.c against port/kz1hd/win_dpi.h (the Windows side runs in tools/wine-host-tests.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu99 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_win_dpi.c" -o "$out/t"
timeout 60 "$out/t"
