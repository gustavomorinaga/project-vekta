#!/usr/bin/env bash
# Builds and runs test_fios_cache.c against port/kz1hd/fios_cache.h in a temporary folder.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
mkdir "$out/fios"
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_fios_cache.c" -o "$out/t"
timeout 60 "$out/t" "$out/fios"
