#!/usr/bin/env bash
# Builds and runs test_fios_cache.c against port/kz1hd/fios_cache.h in a temporary folder: one FIOS folder, and a
# /dev_hdd1/cache with the US and the European disc's ids and a folder with no fios in it.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
mkdir -p "$out/fios" "$out/cache/BCUS98116/fios" "$out/cache/BCES01743/fios" "$out/cache/OTHER"
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_fios_cache.c" -o "$out/t"
timeout 60 "$out/t" "$out/fios" "$out/cache"
