#!/usr/bin/env bash
# Builds and runs test_glow_ref.c against port/kz1hd/native/glow_ref.c (no game data, no GPU).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
CC=${CC:-cc}
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"$CC" -std=c11 -O1 -Wall -Werror -o "$out/test_glow_ref" tools/tests/test_glow_ref.c port/kz1hd/native/glow_ref.c
"$out/test_glow_ref"
