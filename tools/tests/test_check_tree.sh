#!/usr/bin/env bash
# Host test for tools/check-tree.sh: in a scratch repository it must pass a clean tree and catch each thing it is
# there for (a game file by name and by magic, a personal path, a private e-mail, a token, a co-author trailer,
# Portuguese text). No game data: the "game files" are a few made-up bytes.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 2
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
fails=0
fresh() {
  rm -rf "$D/r"; mkdir -p "$D/r/tools"; cp tools/check-tree.sh "$D/r/tools/"
  git -C "$D/r" init -q
  printf 'An English line.\n' > "$D/r/README.md"
}
expect() {   # expect <pass|fail> <what> : runs the check on the scratch tree
  local want=$1 what=$2 out rc
  out=$(sh "$D/r/tools/check-tree.sh" 2>&1); rc=$?
  if [[ $want == pass && $rc != 0 ]] || [[ $want == fail && $rc == 0 ]]; then
    echo "FAIL: $what (exit $rc)"; echo "$out" | head -3; fails=$((fails + 1))
  fi
}
fresh; expect pass "a clean tree"
fresh; printf 'x' > "$D/r/data.psarc"; expect fail "a .psarc by name"
fresh; printf '\177ELF\002\001' > "$D/r/blob.bin"; expect fail "an ELF by magic"
fresh; printf 'PSAR\000\001' > "$D/r/blob2.bin"; expect fail "a PSARC by magic"
fresh; printf 'see /ho%sme/someone/game\n' '' >> "$D/r/README.md"; expect fail "a personal path"
fresh; printf 'mail someone\100example.net\n' >> "$D/r/README.md"; expect fail "a private e-mail address"
fresh; printf 'token = ghp_%s\n' "abcdefghijklmnopqrstuvwxyz0123456789" >> "$D/r/README.md"; expect fail "a GitHub token"
fresh; printf 'Co-Authored-By: An Agent <a@users.noreply.github.com>\n' >> "$D/r/README.md"; expect fail "a co-author trailer"
fresh; printf 'O MLAA medido no pr\303\263prio m\303\263dulo\n' >> "$D/r/README.md"; expect fail "Portuguese text"
fresh; printf 'Field of view: 0.022° × sensitivity, a few µs\n' >> "$D/r/README.md"; expect pass "degree, multiplication and micro signs"
echo "test_check_tree: $([[ $fails == 0 ]] && echo ok || echo FAIL)"
exit $(( fails > 0 ))
