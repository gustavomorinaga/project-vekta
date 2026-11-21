#!/usr/bin/env bash
# Host test for port/kz1hd/heap_bytes.h: the heap count builds and links against the system's C library and, with
# the pinned Zig (KZ_ZIG, else $KZ_WORK/toolchain-cache/zig/zig), against glibc 2.31, the Linux release folder's
# target (port/cmake/zig-linux.cmake), which has no mallinfo2 (KZ1HD's link failed there with "undefined symbol:
# mallinfo2", found from RecompLauncher's "Add a build" on 2026-11-20). CI's checks job has the pinned Zig and sets
# KZ_REQUIRE_QEMU=1, which makes a missing Zig a failure there too; elsewhere it is noted. No game data.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
fails=0
check() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi; }
cat >"$out/t.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#include "heap_bytes.h"
int main(void)
{
    size_t before = kz_heap_bytes();
    void* volatile p = malloc(64 << 10);   /* below the mmap threshold: on the heap */
    size_t after = kz_heap_bytes();
    printf("%zu %zu\n", before, after);
    free(p);
    return after >= before + (64 << 10) ? 0 : 1;
}
C
"${CC:-cc}" -std=gnu17 -O1 -Wall -Werror -I "$ROOT/port/kz1hd" "$out/t.c" -o "$out/t" && "$out/t" >/dev/null
check $? "system C library: builds, links and counts a 64 KB allocation"
zig=${KZ_ZIG:-${KZ_WORK:-$ROOT/work}/toolchain-cache/zig/zig}
if [ -x "$zig" ]; then
  ZIG_GLOBAL_CACHE_DIR="$out/zc" ZIG_LOCAL_CACHE_DIR="$out/zc" "$zig" cc -target x86_64-linux-gnu.2.31 -std=gnu17 -O1 \
    -Wall -Werror -I "$ROOT/port/kz1hd" "$out/t.c" -o "$out/t231" >"$out/zig.log" 2>&1
  r=$?; [ $r = 0 ] || tail -5 "$out/zig.log"
  check $r "glibc 2.31 (the Linux folder's target, pinned Zig): builds and links, no mallinfo2"
  [ $r = 0 ] && { "$out/t231" >/dev/null; check $? "and counts a 64 KB allocation (run on this system's newer glibc)"; }
elif [ "${KZ_REQUIRE_QEMU:-0}" = 1 ]; then
  echo "FAIL the pinned Zig ($zig) not found"; fails=$((fails + 1))
else
  echo "note the pinned Zig ($zig) is not here: the glibc 2.31 link is CI's"
fi
echo "test_heap_bytes: $([ $fails = 0 ] && echo ok || echo "FAIL ($fails)")"
[ $fails = 0 ]
