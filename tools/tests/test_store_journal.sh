#!/usr/bin/env bash
# Builds and runs test_store_journal.c against the patched tree's runtime/ppu/ppu_store_journal.h (patch 3034;
# tools/ps3recomp-patched.sh first), and checks that the lifter emits dcbz through PPU_DCBZ, which a build without
# PS3RECOMP_STORE_JOURNAL expands to the plain memset it replaced.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/runtime/ppu/ppu_store_journal.h" ]] || { echo "FAIL: no ppu_store_journal.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -DPS3RECOMP_STORE_JOURNAL -I "$P/runtime/ppu" \
    "$ROOT/tools/tests/test_store_journal.c" -o "$out/t" -lpthread
timeout 60 "$out/t"
# the lifter: dcbz through PPU_DCBZ; without the option, PPU_DCBZ is the memset
python3 - "$P/tools" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
import ppu_lifter as L
src = open(L.__file__).read()
ok = 'f"PPU_DCBZ(ea); }}")' in src
pre = L.HEADER_PREAMBLE if hasattr(L, "HEADER_PREAMBLE") else src
plain = "#define PPU_DCBZ(ea) memset(vm_base + (uint32_t)(ea), 0, 128)" in pre
journal = "#define PPU_ST_DIAG (g_barrier_sync_watch | g_null_sweep_hi | g_store_journal)" in pre
for c, what in ((ok, "the lifter emits dcbz as PPU_DCBZ"), (plain, "without the option PPU_DCBZ is the plain memset"),
                (journal, "with the option the journal closes the store snapshot (PPU_ST_DIAG)")):
    print(("ok   " if c else "FAIL: ") + what)
sys.exit(0 if ok and plain and journal else 1)
EOF
