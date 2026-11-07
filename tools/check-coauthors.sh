#!/usr/bin/env bash
# Fails when a commit in RANGE carries a Co-Authored-By trailer (AGENTS.md, "Commits and pull requests"), except
# from the automation bots below, which GitHub itself adds when a pull request they opened is squash-merged
# (Dependabot's "ci: bump the actions group" turned main's CI red on 2026-11-07). Only these exact GitHub App
# identities pass: any other bot, and any AI agent (Claude, Copilot, Codex, Cursor and the like, as a [bot] account
# or not), still fails.
#   tools/check-coauthors.sh RANGE      (a git revision range, e.g. BASE..HEAD or HEAD^!)
set -uo pipefail
[[ $# == 1 ]] || { echo "usage: tools/check-coauthors.sh RANGE" >&2; exit 2; }
ALLOWED=(
  "dependabot[bot] <49699333+dependabot[bot]@users.noreply.github.com>"
  "github-actions[bot] <41898282+github-actions[bot]@users.noreply.github.com>"
)
log=$(git log --format='%H%n%B' "$1") || { echo "check-coauthors: cannot read $1" >&2; exit 2; }
bad=0
while IFS= read -r line; do
  who=$(printf '%s' "$line" | sed -E 's/^[Cc][Oo]-[Aa][Uu][Tt][Hh][Oo][Rr][Ee][Dd]-[Bb][Yy]:[[:space:]]*//; s/[[:space:]]+$//')
  ok=0
  for a in "${ALLOWED[@]}"; do [[ "$who" == "$a" ]] && ok=1; done
  if (( ! ok )); then echo "$line"; bad=1; fi
done < <(printf '%s\n' "$log" | grep -i '^co-authored-by:')
if (( bad )); then
  echo "::error::a commit carries a Co-Authored-By trailer (AGENTS.md, Commits and pull requests)"; exit 1
fi
exit 0
