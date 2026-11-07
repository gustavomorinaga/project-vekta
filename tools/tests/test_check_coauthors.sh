#!/usr/bin/env bash
# Host test for tools/check-coauthors.sh: in a scratch repository, a commit with no trailer and Dependabot's or
# GitHub Actions' own trailer pass; an AI agent's trailer (as a person or as a [bot] account), a person's, a look-alike
# of Dependabot with another address, and a bad trailer next to an allowed one fail.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 2
C="$PWD/tools/check-coauthors.sh"
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
git -C "$D" init -q
g() { git -C "$D" -c user.name=t -c user.email=t$'\x40'example.invalid -c commit.gpgsign=false "$@"; }
g commit -q --allow-empty -m base
fails=0
expect() {   # expect <pass|fail> <what> <message>
  local want=$1 what=$2 rc
  g commit -q --allow-empty -m "$3"
  (cd "$D" && bash "$C" 'HEAD^!' >/dev/null 2>&1); rc=$?
  if [[ $want == pass && $rc != 0 ]] || [[ $want == fail && $rc != 1 ]]; then
    echo "FAIL: $what (exit $rc)"; fails=$((fails + 1))
  fi
}
dep='Co-authored-by: dependabot[bot] <49699333+dependabot[bot]@users.noreply.github.com>'
expect pass "no trailer" $'a change\n\nwhy it changed'
expect pass "Dependabot's squash trailer" $'ci: bump\n\n'"$dep"
expect pass "GitHub Actions' trailer" $'x\n\nCo-Authored-By: github-actions[bot] <41898282+github-actions[bot]@users.noreply.github.com>'
expect fail "Claude" $'x\n\nCo-Authored-By: Claude <noreply\x40anthropic.com>'
expect fail "claude[bot]" $'x\n\nCo-authored-by: claude[bot] <209825114+claude[bot]@users.noreply.github.com>'
expect fail "Copilot's agent" $'x\n\nCo-authored-by: copilot-swe-agent[bot] <198982749+Copilot@users.noreply.github.com>'
expect fail "a person" $'x\n\nCo-authored-by: Someone <someone\x40example.invalid>'
expect fail "Dependabot's name with another address" $'x\n\nCo-authored-by: dependabot[bot] <bot\x40example.invalid>'
expect fail "an AI trailer next to Dependabot's" $'x\n\n'"$dep"$'\nCo-Authored-By: Codex <codex\x40openai.com>'
(cd "$D" && bash "$C" >/dev/null 2>&1); [[ $? == 2 ]] || { echo "FAIL: no range is not a usage error"; fails=$((fails + 1)); }
(( fails == 0 )) && echo "test_check_coauthors: ok"
(( fails == 0 ))
