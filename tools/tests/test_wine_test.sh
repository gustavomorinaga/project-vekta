#!/usr/bin/env bash
# Host test for tools/wine-test.sh: its verdict on runs of a stand-in for Wine (KZ_WINE) that print what the game
# prints. A run that keeps presenting frames passes; one that is too slow, crashes, never boots or stops early fails.
# Needs bwrap (the script runs in tools/sandbox.sh); no Wine, no game, no GPU.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 2
# Outside /tmp: the sandbox gives /tmp its own empty tmpfs, so the stand-ins would not be there.
D=$(mktemp -d "${HOME:?}/.vekta-wine-test.XXXXXX"); trap 'rm -rf "$D"' EXIT
mkdir -p "$D/work/vfs/PS3_GAME/USRDIR" "$D/bin"
: > "$D/work/vfs/PS3_GAME/USRDIR/EBOOT.elf"; : > "$D/KZ1HD.exe"
# The stand-in: MODE picks what it prints; FPS frames a second, a [kz-stats] line every 2 s of game time, at once.
cat > "$D/bin/wine" <<'EOF'
#!/bin/sh
[ "$1" = wineboot ] && { echo boot >> "$WINEPREFIX/system.reg"; exit 0; }
exec >> "${KZ_LOG#Z:}"   # the game writes its log to KZ_LOG
[ "$MODE" = noboot ] && { echo "err:module:import_dll Library vulkan-1.dll not found"; exit 1; }
end=60; [ "$MODE" = early ] && end=20
t=0; while [ $t -le $end ]; do
  echo "[kz-stats] t=$((1000 + t))s flips=$((t * FPS)) queued=1 gap=7ms heap=28MB ticks=0"
  [ "$MODE" = crash ] && [ $t = 30 ] && { echo "wine: Unhandled page fault on read access to 0000000000000000"; exit 1; }
  t=$((t + 2))
done
[ "$MODE" = early ] && exit 3
exit 0
EOF
chmod +x "$D/bin/wine"
export KZ_WORK="$D/work" KZ_WINE="$D/bin/wine"
fails=0
check() { local want=$1 what=$2 mode=$3 fps=$4 out rc
  out="$(MODE=$mode FPS=$fps tools/wine-test.sh --seconds 60 "$D/KZ1HD.exe" 2>&1)"; rc=$?
  if [[ $want == pass && $rc == 0 && $out == *"wine-test: PASS"* ]] || [[ $want == fail && $rc == 1 && $out == *"wine-test: FAIL"* ]]
  then :; else echo "FAIL: $what (exit $rc)"; tail -3 <<<"$out"; fails=$((fails + 1)); fi
}
check pass "a run at 60 fps passes"                  ok     60
check fail "a run at 10 fps fails (--min-fps 20)"    ok     10
check fail "a crash in the log fails"                crash  60
check fail "no [kz-stats] lines fails"               noboot 60
check fail "stats that stop after 20 s fail"         early  60
grep -q 'flips=' "$D/work/wine/wine.log" || { echo "FAIL: no log in the work folder"; fails=$((fails + 1)); }
[[ "$(cat "$D/work/wine/prefix/system.reg" 2>/dev/null)" == boot ]] ||
  { echo "FAIL: the prefix was not made once, in the work folder"; fails=$((fails + 1)); }
echo "test_wine_test: $([[ $fails == 0 ]] && echo ok || echo FAIL)"
exit $(( fails > 0 ))
