#!/usr/bin/env bash
# ps3recomp's own tests (every tools/test_*.py of the pinned release) run on the patched tree, so a patch in
# port/patches/ that breaks something upstream tests fails here. Run tools/ps3recomp-patched.sh first; this copies
# work/ps3recomp-patched to a temporary folder and runs the tests there, because some of them write into the tree
# they test. No game data, no GPU, no network: a C and C++ compiler, CMake and sdl2-config (tools/check-deps.sh).
#   tools/tests/test_upstream_suite.sh [-j N] [-v] [TEST ...]   (TEST: a name such as test_gcm_ea2io_range)
#   -j N  tests at the same time (default: the number of cores); the timing tests below always run alone, after
#   -v    a line for every test, not only the failures
# When a patch changes on purpose what an upstream test checks, the patch updates that test (as patch 0004 does);
# nothing is skipped here. (After Project Helghast's tools/upstream/test_upstream_suite.sh, MIT.)
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
J="$(nproc 2>/dev/null || echo 2)"; V=0; only=()
while (( $# )); do
  case "$1" in
    -j) shift; J="${1:-}"; [[ "$J" =~ ^[1-9][0-9]*$ ]] || { echo "-j needs a positive number" >&2; exit 2; } ;;
    -v) V=1 ;;
    -*) echo "usage: tools/tests/test_upstream_suite.sh [-j N] [-v] [TEST ...]" >&2; exit 2 ;;
    *) only+=("${1%.py}") ;;
  esac
  shift
done
# Timed against wall-clock limits (a back-off window, a watchdog): they fail on a loaded machine, so they run alone.
SERIAL=" test_spu_dma_repeat test_spu_event_producers test_spurs_coordinator_kick "

[[ -d "$PS3RECOMP_DIR/tools" ]] || { echo "FAIL: no patched tree at $PS3RECOMP_DIR; run tools/ps3recomp-patched.sh" >&2; exit 1; }
D="$(mktemp -d)"; trap 'rm -rf "$D"' EXIT
mkdir "$D/tree" "$D/out"
tar -C "$PS3RECOMP_DIR" -cf - . | tar -C "$D/tree" -xf -
export PYTHONDONTWRITEBYTECODE=1

tests=()
if (( ${#only[@]} )); then tests=("${only[@]}")
else for f in "$D"/tree/tools/test_*.py; do t="${f##*/}"; tests+=("${t%.py}"); done; fi
(( ${#tests[@]} )) || { echo "FAIL: no tests in $PS3RECOMP_DIR/tools" >&2; exit 1; }

run_one() {   # name -> $D/out/name.{log,rc}
  ( cd "$D/tree" && timeout 600 python3 "tools/$1.py" ) >"$D/out/$1.log" 2>&1
  echo $? >"$D/out/$1.rc"
}
parallel=(); serial=()
for t in "${tests[@]}"; do
  [[ -f "$D/tree/tools/$t.py" ]] || { echo "FAIL: $t is not in $PS3RECOMP_DIR/tools" >&2; exit 1; }
  if [[ "$SERIAL" == *" $t "* ]]; then serial+=("$t"); else parallel+=("$t"); fi
done
running=0
for t in "${parallel[@]}"; do
  while (( running >= J )); do wait -n; running=$((running - 1)); done
  run_one "$t" & running=$((running + 1))
done
wait
for t in "${serial[@]}"; do run_one "$t"; done

failed=()
for t in "${tests[@]}"; do
  rc="$(cat "$D/out/$t.rc")"
  if [[ "$rc" != 0 ]]; then
    failed+=("$t"); echo "FAIL $t (exit $rc)"; tail -15 "$D/out/$t.log" | sed 's/^/    /'
  elif (( V )); then echo "ok   $t"; fi
done
echo "ps3recomp's tests on the patched tree: $(( ${#tests[@]} - ${#failed[@]} ))/${#tests[@]} pass${failed:+; failed: ${failed[*]}}"
(( ${#failed[@]} == 0 ))
