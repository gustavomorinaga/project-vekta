#!/usr/bin/env bash
# Builds and runs test_move_vel.c against port/kz1hd/move_vel.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=c99 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_move_vel.c" -o "$out/t" -lm
timeout 60 "$out/t"
