#!/usr/bin/env bash
# Builds and runs test_flip_phase.c against port/kz1hd/flip_phase.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_flip_phase.c" -o "$out/t" -lm
timeout 60 "$out/t"
