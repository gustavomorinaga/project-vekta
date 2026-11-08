#!/usr/bin/env bash
# Builds and runs test_lua_states.c against port/kz1hd/lua_states.h.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_lua_states.c" -o "$out/t"
timeout 60 "$out/t"
