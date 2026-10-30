#!/usr/bin/env bash
# Host test for tools/sandbox.sh: inside it the repository and a stand-in for the game files are read-only, the work
# folder is writable, /tmp is private and empty of the host's files, and only the loopback network exists.
# Needs bwrap (bubblewrap); fails without it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 2
# Outside /tmp, as real game files are (the sandbox gives /tmp its own empty tmpfs).
D=$(mktemp -d "${HOME:?}/.vekta-sandbox-test.XXXXXX"); H=$(mktemp /tmp/vekta-sandbox-host.XXXXXX)
trap 'rm -rf "$D" "$H"' EXIT
mkdir -p "$D/game" "$D/work"; echo game > "$D/game/EBOOT.elf"
export KZ_WORK="$D/work" KZ_GAME="$D/game"
fails=0
run() { tools/sandbox.sh -- sh -c "$1" >/dev/null 2>&1; }
expect() { local want=$1 what=$2 cmd=$3
  if run "$cmd"; then got=ok; else got=refused; fi
  [[ $got == "$want" ]] || { echo "FAIL: $what (got $got)"; fails=$((fails + 1)); }
}
expect ok      "the work folder is writable"            "echo x > '$D/work/new' && test -s '$D/work/new'"
expect ok      "the game files are readable"            "grep -q game '$D/game/EBOOT.elf'"
expect refused "the game files are read-only"           "echo x >> '$D/game/EBOOT.elf'"
expect refused "no new file next to the game files"     "touch '$D/game/new'"
expect refused "the repository is read-only"            "touch '$PWD/sandbox-probe'"
expect ok      "/tmp is writable"                       "echo x > /tmp/probe"
expect refused "/tmp does not show the host's /tmp"     "test -e '$H'"
expect ok      "only the loopback interface"            "test \"\$(grep -c : /proc/net/dev)\" = 1"
expect ok      "caches go to the work folder"           "test \"\$XDG_CACHE_HOME\" = '$D/work/cache'"
[[ -e sandbox-probe ]] && { rm -f sandbox-probe; echo "FAIL: a file appeared in the repository"; fails=$((fails + 1)); }
[[ "$(cat "$D/game/EBOOT.elf")" == game ]] || { echo "FAIL: the game file changed"; fails=$((fails + 1)); }
echo "test_sandbox: $([[ $fails == 0 ]] && echo ok || echo FAIL)"
exit $(( fails > 0 ))
