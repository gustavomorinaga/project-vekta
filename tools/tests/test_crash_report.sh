#!/usr/bin/env bash
# Host test for the crash reports, the hang watchdog, KZ_CRASH_TEST and the session header (port/kz1hd/
# crash_report.c, sys_info.c; docs/dev/crash-reports.md): tools/tests/test_crash_report.c, built like the game's own
# objects (-O2, no frame pointers) with ps3recomp's ps3_stdio.c and win32_compat.c from the patched tree
# (tools/ps3recomp-patched.sh first), runs itself once per case and checks each report, its copy in KZ_CRASH_DIR, the
# signature on a second run and the exit status. Built with clang and with gcc when both are here (CC: just that
# one). Then, for frame #0 of the null-pointer crash, nm and addr2line on the same program agree with the
# module+offset the report wrote (what the player's support package will do). The Windows build of the same program
# runs under Wine in tools/wine-host-tests.sh. No game data, no GPU.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/runtime/platform/ps3_stdio.c" ]] || { echo "FAIL: no patched tree at $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
fails=0
check() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi; }
K="$ROOT/port/kz1hd"
ccs=()
if [ -n "${CC:-}" ]; then ccs=("$CC"); else
  for c in clang gcc; do command -v "$c" >/dev/null && ccs+=("$c"); done
fi
[ ${#ccs[@]} -gt 0 ] || { echo "FAIL: no C compiler (clang, gcc or CC)" >&2; exit 1; }
for cc in "${ccs[@]}"; do
  d="$out/$cc"; mkdir -p "$d"
  "$cc" -std=gnu17 -O2 -g -fomit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Werror -I "$K" -I "$P/include" \
    -I "$P/runtime/platform" "$ROOT/tools/tests/test_crash_report.c" "$K/crash_report.c" "$K/sys_info.c" \
    "$P/runtime/platform/ps3_stdio.c" "$P/runtime/platform/win32_compat.c" -pthread -ldl -o "$d/test_crash_report" \
    >"$d/build.log" 2>&1
  r=$?; [ $r = 0 ] || tail -20 "$d/build.log"
  check $r "$cc: builds with -Werror (-O2, no frame pointers)"
  [ $r = 0 ] || continue
  "$d/test_crash_report" "$d/run" >"$d/run.log" 2>&1
  r=$?
  grep '^FAIL' "$d/run.log"
  check $r "$cc: test_crash_report ($(grep -c '^ok' "$d/run.log") checks ok, $(grep -c '^FAIL' "$d/run.log") failed)"
  # frame #0 of the null-pointer read, symbolized from module+offset alone as the support package will
  f0=$(sed -n 's/^\[kz-crash\] #0 test_crash_report+\(0x[0-9a-f]*\)$/\1/p' "$d/run/null-main-1.log" | head -1)
  sym=""
  if [ -n "$f0" ] && command -v addr2line >/dev/null; then sym=$(addr2line -f -e "$d/test_crash_report" "$f0" | head -1); fi
  r=1; [ "$sym" = kz_crash_null_read ] && r=0
  check $r "$cc: addr2line on frame #0 ($f0) names kz_crash_null_read (got '$sym')"
done
echo "test_crash_report: $([ $fails = 0 ] && echo ok || echo "FAIL ($fails)")"
[ $fails = 0 ]
