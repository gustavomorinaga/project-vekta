#!/usr/bin/env bash
# Builds and runs test_hud_sites.c against port/kz1hd/native_ppu/hud_sites.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd/native_ppu" "$ROOT/tools/tests/test_hud_sites.c" -o "$out/t"
timeout 60 "$out/t"
