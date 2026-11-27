#!/usr/bin/env bash
# Builds and runs test_trophy_list.cpp against port/kz1hd/trophy_list.h (no GPU, no game data).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_trophy_list.cpp" -o "$out/t"
timeout 60 "$out/t"
