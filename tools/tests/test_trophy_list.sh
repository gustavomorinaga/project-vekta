#!/usr/bin/env bash
# Builds and runs test_trophy_list.cpp against port/kz1hd/trophy_list.h (no GPU, no game data).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CXX:-c++}" -std=c++17 -O1 -UNDEBUG -Wall -Werror -I "$ROOT/port/kz1hd" "$ROOT/tools/tests/test_trophy_list.cpp" -o "$out/t"
timeout 60 "$out/t"

# The page's script (pcoptions.cpp): rows are cells aligned left (0) and right (2), as the game's Statistics page.
# An AddTextItem keeps align -1, whose first line the game centres in a box of the unscaled width while the glyphs
# are drawn narrower (16:9), so longer rows started further right. No selection indent; no count card on the page.
src="$ROOT/port/kz1hd/pcoptions.cpp"
page=$(sed -n '/"if not KZTRMAKE then/,/^    "end\\n"$/p' "$src")
fail() { echo "FAIL: $1" >&2; exit 1; }
grep -q "SetAlignedTextItem(i - 1, 0, d\[i\].l, 0)" <<<"$page" || fail "the grade and name are a left-aligned cell"
grep -q "SetAlignedTextItem(i - 1, 1, d\[i\].v, 2)" <<<"$page" || fail "the day or Locked is a right-aligned cell"
grep -q "SetColumn(1, B.B2.item_width - 40 - 100, 1" <<<"$page" || fail "the day's column ends at the list's right end"
grep -q "AddListBox(B.B2.item_x + 20," <<<"$page" || fail "the rows start under the page title"
grep -q "SetSelectionIndent(0)" <<<"$page" || fail "no selection indent"
! grep -q "AddTextItem" <<<"$page" || fail "no AddTextItem rows (they drift right with their width)"
! grep -q "\.v \.\." <<<"$page" || fail "the day is not under the list"
! grep -rq "TrophyPing\|kz_trophy_page_ping" "$ROOT/port/kz1hd" || fail "no count card on the page"
grep -A1 "RIGHT, CENTER" <<<"$page" | grep -q "d\[i\].v, 2)" || fail "the day's cell is made under the RIGHT format"
grep -q "SetTextMultilineAllowed(i - 1, 0, FALSE)" <<<"$page" || fail "a long name stays on its row"
echo "trophy page script: ok"
