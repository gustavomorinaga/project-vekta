#!/usr/bin/env bash
# Runs every test in tools/host-tests.txt from the repository root, one after the other. Prints one line per test and
# a summary; exits 1 if any failed. Needs no game data and no GPU: bash, python3 and a C compiler (CC, default cc).
#   --gh   wrap each test's output in a GitHub Actions log group (CI)
# (After Project Helghast's tools/run-host-tests.sh, MIT; this one is sequential.)
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2
GH=0
case "${1:-}" in --gh) GH=1 ;; "") ;; *) echo "usage: tools/run-host-tests.sh [--gh]" >&2; exit 2 ;; esac
out=$(mktemp); trap 'rm -f "$out"' EXIT
n=0; failed=()
while read -r t; do
  [[ -z "$t" || "$t" == \#* ]] && continue
  n=$((n + 1))
  if [[ ! -f "$t" ]]; then echo "FAIL $t (listed but missing)"; failed+=("$t"); continue; fi
  case "$t" in *.py) run=(python3 "$t") ;; *) run=(bash "$t") ;; esac
  "${run[@]}" >"$out" 2>&1; rc=$?
  if (( GH )); then echo "::group::$t"; cat "$out"; echo "::endgroup::"; fi
  if (( rc )); then
    failed+=("$t")
    (( GH )) && echo "::error::host test failed: $t" || { echo "FAIL $t"; tail -5 "$out"; }
  elif (( ! GH )); then
    echo "ok   $t"
  fi
done < tools/host-tests.txt
echo "host tests: $((n - ${#failed[@]}))/$n pass${failed:+; failed: ${failed[*]}}"
(( ${#failed[@]} == 0 ))
