#!/usr/bin/env bash
# Builds and runs test_pgo_write.c against port/kz1hd/pgo_write.h, with and without a profile runtime.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
for v in WITH_RUNTIME NO_RUNTIME; do
    "${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -D"$v" -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_pgo_write.c" \
        -o "$out/t-$v"
    timeout 60 "$out/t-$v"
done
