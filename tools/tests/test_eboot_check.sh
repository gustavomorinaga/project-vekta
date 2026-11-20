#!/usr/bin/env bash
# Host test for the EBOOT check (port/kz1hd/eboot_check.h, main.cpp's kz_eboot_verify, tools/kzbuild.py's
# write_eboot_id): test_eboot_check.c (SHA-256 vectors from hashlib, equal / one byte changed / truncated / missing, in
# a folder with a space and accents); its hash of a random 3 MB file equals hashlib's; write_eboot_id writes the size
# and hash of a stand-in EBOOT and nothing of its content; main.cpp checks before ppu_load_elf; launch.py's EXIT_EBOOT
# is kz_exit.h's KZ_EXIT_EBOOT. The Windows build runs under Wine in tools/wine-host-tests.sh. No game data.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
K="$ROOT/port/kz1hd"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
d="$out/Ações e EBOOT"; mkdir -p "$d"
fails=0
check() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi; }
is() { local what=$1; shift; "$@"; check $? "$what"; }   # is WHAT TEST...: the test's result as a check
CC=${CC:-cc}
"$CC" -std=gnu17 -O2 -Wall -Wextra -Werror -I "$K" "$ROOT/tools/tests/test_eboot_check.c" -o "$out/t" ||
  { echo "FAIL: test_eboot_check.c does not build" >&2; exit 1; }
"$out/t" "$d" >"$out/t.log"; check $? "test_eboot_check: $(tail -1 "$out/t.log")"
grep '^FAIL' "$out/t.log"

cat >"$out/hash.c" <<C
#include "eboot_check.h"
int main(int c, char** v) { long long n; char h[65]; (void)c; kz_eboot_check(v[1], "", 3 << 20, &n, h); puts(h); return 0; }
C
"$CC" -std=gnu17 -O2 -I "$K" "$out/hash.c" -o "$out/hash"
head -c $((3 << 20)) /dev/urandom >"$d/r.bin"
is "a random 3 MB file: the same SHA-256 as hashlib's" \
  [ "$("$out/hash" "$d/r.bin")" = "$(python3 -c 'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$d/r.bin")" ]

python3 - "$ROOT" "$d" <<'PY'; check $? "write_eboot_id writes the size and SHA-256 of the EBOOT it lifts, and nothing of it"
import hashlib, os, re, sys
sys.path.insert(0, sys.argv[1] + "/tools")
import kzbuild
e = os.path.join(sys.argv[2], "EBOOT.elf")
data = b"\x7fELF" + bytes(range(256)) * 4000
open(e, "wb").write(data)
gen = os.path.join(sys.argv[2], "gen")
hx, size = kzbuild.write_eboot_id(e, gen)
text = open(os.path.join(gen, "eboot_id.h")).read()
assert hx == hashlib.sha256(data).hexdigest() and size == len(data), (hx, size)
assert f'#define KZ_EBOOT_SHA256 "{hx}"' in text and f"#define KZ_EBOOT_SIZE {len(data)}LL" in text, text
assert len(text) < 400 and re.findall(r"#define (\w+)", text) == ["KZ_EBOOT_SHA256", "KZ_EBOOT_SIZE"], text
src = open(sys.argv[1] + "/tools/kzbuild.py").read()
assert 'write_eboot_id(eboot, j(k, "gen"))' in src, "cmd_lift writes it into the lift's gen/"
PY

main="$K/main.cpp"
v=$(grep -n 'kz_eboot_verify(argv\[1\])' "$main" | cut -d: -f1); l=$(grep -n 'ppu_load_elf(argv\[1\])' "$main" | cut -d: -f1)
r=1; [ -n "$v" ] && [ -n "$l" ] && [ "$v" -lt "$l" ] && r=0
check $r "main.cpp checks the EBOOT before ppu_load_elf (lines $v, $l)"
grep -q 'getenv("KZ_EBOOT_CHECK")' "$main" && grep -q '__has_include("eboot_id.h")' "$main"
check $? "KZ_EBOOT_CHECK=0 skips it; a lift without eboot_id.h still builds"
want=$(sed -n 's/^#define KZ_EXIT_EBOOT *\([0-9]*\).*/\1/p' "$K/kz_exit.h")
is "launch.py's EXIT_EBOOT is kz_exit.h's KZ_EXIT_EBOOT ($want)" \
  [ "$(sed -n 's/^EXIT_EBOOT = \([0-9]*\)$/\1/p' "$ROOT/tools/release/launch.py")" = "${want:-none}" ]
echo "test_eboot_check: $([ $fails = 0 ] && echo ok || echo "FAIL ($fails)")"
[ $fails = 0 ]
