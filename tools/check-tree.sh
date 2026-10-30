#!/bin/sh
# Checks the files git tracks (or would track: staged and untracked-not-ignored) for what must never be published:
# game files by type or by magic, big binaries, personal paths, e-mail addresses other than the public one, keys
# and tokens, and text that is not English. Exits 1 with the findings. CI runs it on every push; run it before a PR.
set -u
KZ_ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)
cd "$KZ_ROOT" || exit 1
fail=0
note() { echo "check-tree: $*"; fail=1; }
FILES=$(git ls-files --cached --others --exclude-standard | grep -v '^third_party/')

# Game files by name and by content (ELF/SELF/PKG/PSARC/SFO magics), and anything over 4 MB.
for f in $FILES; do
    [ -f "$f" ] || continue
    case "$f" in
        *.elf|*.self|*.sprx|*.prx|*.pic|*.pkg|*.PKG|*.edat|*.sdat|*.rap|*.psarc|*.core|*.snz|*.avi|*.at3|*.TRP|*.SFO|*EBOOT.BIN)
            note "$f: a game file type" ;;
    esac
    magic=$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')
    case "$magic" in
        7f454c46) note "$f: an ELF binary" ;;
        53434500) note "$f: a SELF/SCE container" ;;
        7f504b47) note "$f: a PKG" ;;
        50534152) note "$f: a PSARC archive" ;;
        00505346) note "$f: a PARAM.SFO" ;;
    esac
    size=$(wc -c < "$f")
    [ "$size" -gt 4194304 ] && note "$f: $size bytes (over 4 MB)"
done

# Text checks on everything but images.
TEXT=$(printf '%s\n' $FILES | grep -v -E '\.(png|jpg|ico|ttf|woff2)$' | grep -v '^tools/check-tree.sh$')
printf '%s\n' $TEXT | xargs -r grep -n -I -E '/mnt/[A-Z]|/home/[a-z]|C:\\\\Users\\\\' 2>/dev/null | sed 's/^/check-tree: personal path: /' | grep . && fail=1
printf '%s\n' $TEXT | xargs -r grep -n -I -o -E '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[a-z]{2,}' 2>/dev/null |
    grep -v -E '@(gustavomorinaga\.dev|users\.noreply\.github\.com|example\.(com|org))$|@[0-9]x\.(png|jpg|svg)$' |
    sed 's/^/check-tree: e-mail address: /' | grep . && fail=1
printf '%s\n' $TEXT | xargs -r grep -n -I -i -E '(klic|api[_-]?key|secret|token|password)[^a-z0-9]{0,4}[:=][^a-z0-9]{0,4}[0-9a-f]{24,}|gh[pousr]_[A-Za-z0-9]{30,}|sk-[A-Za-z0-9]{30,}' 2>/dev/null |
    sed 's/^/check-tree: key or token: /' | grep . && fail=1
# No co-author trailers (patch headers included): AI use is described once, in the README.
printf '%s\n' $TEXT | xargs -r grep -n -I -i '^Co-Authored-By:' 2>/dev/null |
    sed 's/^/check-tree: co-author trailer: /' | grep . && fail=1
# Portuguese is the language most of the early notes were written in: catch what was left untranslated.
printf '%s\n' $TEXT | LC_ALL=C.UTF-8 xargs -r grep -n -I -E '[ãõçóéíú]|ção|ções| não | para o | para a | uma | está | também ' 2>/dev/null |
    sed 's/^/check-tree: not English: /' | cut -c1-200 | grep . && fail=1

[ "$fail" = 0 ] && echo "check-tree: clean ($(printf '%s\n' $FILES | wc -l) files)"
exit "$fail"
