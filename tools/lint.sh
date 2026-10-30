#!/usr/bin/env bash
# The lint CI runs: shellcheck (warning level) on every tracked shell script, ruff (ruff.toml) on tools/ and re/.
# Exits 1 on any finding. Needs shellcheck and ruff on PATH (`pip install ruff shellcheck-py`, or the distribution's
# packages). Never touches port/patches (upstream style) or third_party/.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2
for t in shellcheck ruff; do command -v "$t" >/dev/null || { echo "lint: $t not found" >&2; exit 2; }; done
found=0
mapfile -t sh < <(git ls-files '*.sh')
(( ${#sh[@]} )) && { shellcheck -S warning -f gcc "${sh[@]}" || found=1; }
ruff check --output-format "${RUFF_FORMAT:-concise}" tools re || found=1
(( found )) && echo "lint: findings above" || echo "lint: clean"
exit "$found"
