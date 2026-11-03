#!/usr/bin/env bash
# The Linux folder's compiler wrappers (tools/release/zig/cc, c++) pass Zig the glibc 2.31 target and every argument
# unchanged, except -march's value, which they spell Zig's way (x86-64-v3 -> x86_64_v3; the port builds with
# -march=x86-64-v3 by default and zig cc refuses that spelling). A stand-in zig prints what it was given.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
printf '#!/bin/sh\nfor a; do printf "[%%s]" "$a"; done; echo\n' > "$t/zig"; chmod +x "$t/zig"
fails=0
check() { local want=$1; shift
  got=$(KZ_ZIG="$t/zig" KZ_WORK="$t" sh "$@")
  [[ "$got" == "$want" ]] || { echo "FAIL: $* gave $got, want $want"; fails=$((fails + 1)); }
}
check "[cc][-target][x86_64-linux-gnu.2.31][-Wno-error=date-time][-O1][-march=x86_64_v3][a b][-c][x.c]" \
  "$ROOT/tools/release/zig/cc" -O1 -march=x86-64-v3 "a b" -c x.c
check "[c++][-target][x86_64-linux-gnu.2.31][-Wno-error=date-time][-march=native][-mtune=x86-64][-o][y]" \
  "$ROOT/tools/release/zig/c++" -march=native -mtune=x86-64 -o y
check "[cc][-target][x86_64-linux-gnu.2.31][-Wno-error=date-time]" "$ROOT/tools/release/zig/cc"
(( fails == 0 )) && echo "test_zig_wrappers: ok" || { echo "test_zig_wrappers: FAIL"; exit 1; }
