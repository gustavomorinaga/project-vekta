#!/usr/bin/env bash
# tools/run-host-tests.sh, run on a made-up list in a temporary tree: one after the other it prints a line per test,
# names the failed and the missing ones and exits 1; --except leaves a listed test out and refuses one that is not
# listed; --gh wraps each output in a log group and marks the failure; with -j the tests run at the same time (two
# of them only pass when each sees the other running), their lines still come in the list's order, and a test named
# in ALONE runs after them with nothing beside it.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/tools/tests" "$T/marks"
cp "$ROOT/tools/run-host-tests.sh" "$T/tools/"
fails=0
check() { if ! eval "$1"; then fails=$((fails + 1)); echo "FAIL: $2"; fi; }

cat >"$T/tools/tests/ok.sh" <<'EOF'
echo "ok.sh ran"
EOF
cat >"$T/tools/tests/ok.py" <<'EOF'
print("ok.py ran")
EOF
cat >"$T/tools/tests/bad.sh" <<'EOF'
echo "bad.sh: what went wrong"; exit 3
EOF
# a.sh and b.sh: each leaves a mark while it runs and passes only once it sees the other's (WAIT x 0.05 s at most)
for me in a b; do
  other=$([[ $me == a ]] && echo b || echo a)
  cat >"$T/tools/tests/$me.sh" <<EOF
m="$T/marks"; touch "\$m/$me"; trap 'rm -f "\$m/$me"' EXIT
for _ in \$(seq \${WAIT:-100}); do [[ -e "\$m/$other" || -e "\$m/$other.seen" ]] && { touch "\$m/$me.seen"; exit 0; }; sleep 0.05; done
echo "$me.sh: $other.sh never ran beside it"; exit 1
EOF
done
# the name run-host-tests.sh keeps in ALONE: fails when a.sh or b.sh is still running
cat >"$T/tools/tests/test_gcm_flip_wait.sh" <<EOF
[[ -e "$T/marks/a" || -e "$T/marks/b" ]] && { echo "ran beside another test"; exit 1; }
[[ -e "$T/marks/a.seen" && -e "$T/marks/b.seen" ]] || { echo "ran before the others"; exit 1; }
echo "alone"
EOF
list() { printf '# a comment\n\n'; printf 'tools/tests/%s\n' "$@"; }
# shellcheck disable=SC2034  # rc and out are read in the conditions check evaluates
run() { ( cd "$T" && bash tools/run-host-tests.sh "$@" ) >"$T/out" 2>&1; rc=$?; out="$(cat "$T/out")"; }

# one after the other: a line each, the failed and the missing ones named, exit 1
list ok.sh ok.py bad.sh gone.sh >"$T/tools/host-tests.txt"
run
check '[[ $rc == 1 ]]' "a failed test makes the exit status 1 (it was $rc)"
check '[[ "$out" == *"ok   tools/tests/ok.sh"*"ok   tools/tests/ok.py"*"FAIL tools/tests/bad.sh"* ]]' "a line per test, in order: $out"
check '[[ "$out" == *"bad.sh: what went wrong"* && "$out" != *"ok.sh ran"* ]]' "only a failed test shows its output: $out"
check '[[ "$out" == *"FAIL tools/tests/gone.sh (listed but missing)"* ]]' "a listed test that is not there fails: $out"
check '[[ "$out" == *"host tests: 2/4 pass; failed: tools/tests/bad.sh tools/tests/gone.sh" ]]' "the summary: $out"

# --except: the test is left out; one that is not listed is refused
run --except tools/tests/bad.sh --except tools/tests/gone.sh
check '[[ $rc == 0 && "$out" == *"host tests: 2/2 pass" && "$out" != *bad.sh* ]]' "--except leaves the tests out: $rc $out"
run --except tools/tests/nope.sh
check '[[ $rc == 2 && "$out" == *"--except tools/tests/nope.sh is not in tools/host-tests.txt"* ]]' "--except of an unlisted test: $rc $out"
run -j 0
check '[[ $rc == 2 && "$out" == usage:* ]]' "-j 0 is refused: $rc $out"
run --what
check '[[ $rc == 2 && "$out" == usage:* ]]' "an unknown option is refused: $rc $out"

# --gh: every output in a group, the failure marked
run --gh --except tools/tests/gone.sh
check '[[ $rc == 1 && "$out" == *"::group::tools/tests/ok.sh"*"ok.sh ran"*"::endgroup::"*"::error::host test failed: tools/tests/bad.sh"* ]]' "--gh: $out"

# -j: a.sh and b.sh at the same time, the lines in the list's order, the ALONE test after them and by itself
list test_gcm_flip_wait.sh a.sh ok.sh b.sh >"$T/tools/host-tests.txt"
run -j 3
check '[[ $rc == 0 && "$out" == *"host tests: 4/4 pass" ]]' "-j 3: the tests run at the same time and the ALONE one after them: $out"
check '[[ "$out" == "ok   tools/tests/a.sh"*"ok   tools/tests/ok.sh"*"ok   tools/tests/b.sh"*"ok   tools/tests/test_gcm_flip_wait.sh"* ]]' "-j 3: the lines come in the list's order, the ALONE test last: $out"
rm -f "$T"/marks/*
WAIT=4 run
check '[[ $rc == 1 && "$out" == *"FAIL tools/tests/a.sh"* ]]' "without -j the tests do not run at the same time: $rc $out"

echo "test_run_host_tests: $([[ $fails == 0 ]] && echo ok || echo FAIL)"
(( fails == 0 ))
