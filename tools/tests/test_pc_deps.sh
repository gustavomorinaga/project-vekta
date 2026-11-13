#!/usr/bin/env bash
# Builds and runs test_pc_deps.c against port/kz1hd/pc_deps.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_pc_deps.c" -o "$out/t"
timeout 60 "$out/t"
