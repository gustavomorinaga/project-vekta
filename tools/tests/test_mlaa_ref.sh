#!/usr/bin/env bash
# Builds and runs test_mlaa_ref.c against port/kz1hd/native/mlaa_ref.c (no game data, no GPU).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
CC=${CC:-cc}
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"$CC" -std=c11 -O1 -frounding-math -Wall -o "$out/test_mlaa_ref" tools/tests/test_mlaa_ref.c port/kz1hd/native/mlaa_ref.c -lm
"$out/test_mlaa_ref"
