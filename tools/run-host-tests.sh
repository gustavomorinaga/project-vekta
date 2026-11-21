#!/usr/bin/env bash
# Runs every test in tools/host-tests.txt from the repository root. Prints one line per test and a summary; exits 1 if
# any failed. Needs no game data and no GPU: bash, python3 and a C compiler (CC, default cc).
#   -j N          N tests at the same time (default 1: one after the other). The tests in ALONE below still run one
#                 at a time, after the others.
#   --gh          wrap each test's output in a GitHub Actions log group (CI)
#   --except T    leave the listed test T out; may be given more than once. CI's checks job leaves out
#                 tools/tests/test_upstream_suite.sh, which has a job of its own (.github/workflows/ci.yml).
# (After Project Helghast's tools/run-host-tests.sh, MIT.)
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2
usage() { echo "usage: tools/run-host-tests.sh [-j N] [--gh] [--except TEST]..." >&2; exit 2; }
GH=0; J=1; except=()
while (( $# )); do
  case "$1" in
    --gh) GH=1 ;;
    -j) shift; J="${1:-}"; [[ "$J" =~ ^[1-9][0-9]*$ ]] || usage ;;
    --except) shift; [[ -n "${1:-}" ]] || usage; except+=("$1") ;;
    *) usage ;;
  esac
  shift
done
# Never beside another test, because a busy machine fails them: ps3recomp's suite runs its own tests on every core
# and times some of them against the wall clock, and test_gcm_flip_wait.sh checks that naps end within 300 us of
# their time (it failed 2 runs in 3 with 8 and 16 tests at once on 4 cores).
ALONE=" tools/tests/test_upstream_suite.sh tools/tests/test_gcm_flip_wait.sh "

tests=(); left=" "
while read -r t; do
  [[ -z "$t" || "$t" == \#* ]] && continue
  skip=0
  for x in ${except[@]+"${except[@]}"}; do [[ "$t" == "$x" ]] && { skip=1; left+="$x "; }; done
  (( skip )) || tests+=("$t")
done < tools/host-tests.txt
for x in ${except[@]+"${except[@]}"}; do
  [[ "$left" == *" $x "* ]] || { echo "run-host-tests: --except $x is not in tools/host-tests.txt" >&2; exit 2; }
done

D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
run_one() {   # index -> $D/INDEX.out and $D/INDEX.rc
  local t="${tests[$1]}"
  if [[ ! -f "$t" ]]; then echo missing >"$D/$1.rc"; return; fi
  case "$t" in *.py) python3 "$t" ;; *) bash "$t" ;; esac >"$D/$1.out" 2>&1
  echo $? >"$D/$1.rc"
}
failed=()
report() {   # index: the test's line (and, with --gh, its output in a group)
  local t="${tests[$1]}" rc
  rc=$(cat "$D/$1.rc")
  if [[ "$rc" == missing ]]; then echo "FAIL $t (listed but missing)"; failed+=("$t"); return; fi
  if (( GH )); then echo "::group::$t"; cat "$D/$1.out"; echo "::endgroup::"; fi
  if [[ "$rc" != 0 ]]; then
    failed+=("$t")
    if (( GH )); then echo "::error::host test failed: $t"; else echo "FAIL $t"; tail -5 "$D/$1.out"; fi
  elif (( ! GH )); then
    echo "ok   $t"
  fi
}

together=(); alone=()
for i in "${!tests[@]}"; do
  if (( J > 1 )) && [[ "$ALONE" != *" ${tests[$i]} "* ]]; then together+=("$i"); else alone+=("$i"); fi
done
running=0
for i in ${together[@]+"${together[@]}"}; do
  while (( running >= J )); do wait -n; running=$((running - 1)); done
  run_one "$i" & running=$((running + 1))
done
wait
for i in ${together[@]+"${together[@]}"}; do report "$i"; done
for i in ${alone[@]+"${alone[@]}"}; do run_one "$i"; report "$i"; done

n=${#tests[@]}
echo "host tests: $((n - ${#failed[@]}))/$n pass${failed:+; failed: ${failed[*]}}"
(( ${#failed[@]} == 0 ))
