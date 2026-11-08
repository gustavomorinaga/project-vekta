#!/usr/bin/env bash
# Builds and runs test_debug_give.cpp against port/kz1hd/debug_give.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_debug_give.cpp" -o "$out/t"
timeout 60 "$out/t"
