#!/usr/bin/env bash
# Builds and runs test_intro_skip.c against port/kz1hd/intro_skip.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=c99 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_intro_skip.c" -o "$out/t"
timeout 60 "$out/t"
