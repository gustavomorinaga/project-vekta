#!/usr/bin/env bash
# Make $PS3RECOMP_DIR (default work/ps3recomp-patched): a fresh copy of third_party/ps3recomp at its pinned commit
# with every patch in port/patches/series applied, in that order. The lift (tools/lift.sh) and the builds use it,
# so the submodule itself stays clean. Adapted from Project Helghast's tools/ps3recomp-patched.sh (undecuple, MIT):
# the line-ending check, the Python lookup and the content-only sync are theirs; the series file is ours, because
# our patches are numbered by area but depend on the order they were written in.
set -euo pipefail
export LC_ALL=C   # git's messages are matched below
KZ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$KZ_ROOT/tools/env.sh"
cd "$KZ_ROOT"
SUB=third_party/ps3recomp
PT="$PS3RECOMP_DIR"
[[ -f "$SUB/CMakeLists.txt" ]] || { echo "ps3recomp-patched: $SUB is empty; run: git submodule update --init" >&2; exit 1; }

# Git for Windows' core.autocrlf=true checks the submodule out with CRLF and the patches stop applying part-way.
# (Not a git checkout, as in a source release: no index to ask, so nothing to check.)
crlf="$( { git -C "$SUB" ls-files --eol 2>/dev/null || true; } | awk '$1 == "i/lf" && $2 == "w/crlf" { n++ } END { print n + 0 }')"
if (( crlf > 0 )); then
  cat >&2 <<EOF
ps3recomp-patched: $crlf file(s) in $SUB are checked out with Windows (CRLF) line endings but stored with LF,
so the patches would not apply. Turn core.autocrlf off for this repository and the submodule, then write the
submodule's files out again (this discards any edits in it):
  git config core.autocrlf false
  git -C $SUB config core.autocrlf false
  git -C $SUB rm -r -q --cached . && git -C $SUB reset -q --hard
EOF
  exit 1
fi

PYTHON=()
for c in "python3" "python" "py -3"; do
  read -r -a cand <<<"$c"
  if command -v "${cand[0]}" >/dev/null 2>&1 &&
     "${cand[@]}" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 6) else 1)' >/dev/null 2>&1; then
    PYTHON=("${cand[@]}"); break
  fi
done
(( ${#PYTHON[@]} )) || { echo "ps3recomp-patched: no Python 3.6 or newer found (python3, python, py -3)" >&2; exit 1; }

# Every patch file is in the series and every series line is a file: a patch left out would be silently skipped.
mapfile -t SERIES < <(grep -v -e '^#' -e '^[[:space:]]*$' port/patches/series)
for p in port/patches/*.patch; do
  printf '%s\n' "${SERIES[@]}" | grep -qxF "$(basename "$p")" || { echo "ps3recomp-patched: $p is not in port/patches/series" >&2; exit 1; }
done

# Patch a scratch copy, then copy over only the files whose content changed, so an unchanged file keeps its mtime
# and the build recompiles only what the patch set changed. No git metadata in the copy.
NEW="$PT.new"
rm -rf "$NEW"
mkdir -p "$NEW"
tar -C "$SUB" --exclude=.git -cf - . | tar -C "$NEW" -xmf -
for name in "${SERIES[@]}"; do
  p="port/patches/$name"
  [[ -f "$p" ]] || { echo "ps3recomp-patched: series names $name, which is not in port/patches/" >&2; exit 1; }
  # Inside the repository, --directory from its root: a bare git apply in the copy would skip paths outside it.
  # Outside it (PS3RECOMP_DIR elsewhere), from the copy itself.
  if [[ "$NEW" == "$KZ_ROOT"/* ]]; then
    out="$(git apply --verbose --whitespace=nowarn --directory="${NEW#"$KZ_ROOT"/}" "$p" 2>&1)" || { echo "$out" >&2; exit 1; }
  else
    out="$(cd "$NEW" && git apply --verbose --whitespace=nowarn "$KZ_ROOT/$p" 2>&1)" || { echo "$out" >&2; exit 1; }
  fi
  grep -q "^Applied patch .* cleanly" <<<"$out" || { echo "patch $p did not apply: $out" >&2; exit 1; }
done
echo "applied ${#SERIES[@]} patches"
mkdir -p "$PT"
"${PYTHON[@]}" - "$NEW" "$PT" <<'PY'
import filecmp, os, shutil, sys
new, pt = sys.argv[1:]
changed = removed = 0
for d, _, files in os.walk(new):
    rel = os.path.relpath(d, new)
    os.makedirs(os.path.join(pt, rel), exist_ok=True)
    for f in files:
        a, b = os.path.join(d, f), os.path.join(pt, rel, f)
        if not os.path.exists(b) or not filecmp.cmp(a, b, shallow=False):
            shutil.copyfile(a, b); shutil.copymode(a, b); changed += 1
for d, _, files in os.walk(pt):
    rel = os.path.relpath(d, pt)
    for f in files:
        if rel == "." and f == ".patchset":
            continue
        if not os.path.exists(os.path.join(new, rel, f)):
            os.remove(os.path.join(d, f)); removed += 1
print(f"synced: {changed} file(s) changed, {removed} removed")
PY
rm -rf "$NEW"
# The patch set this tree was made from, so a build can tell a stale tree.
# The pinned commit: the submodule's HEAD, or in a source release (no git) the one tools/release/pack_source.py wrote.
pin="$(git -C "$SUB" rev-parse --short HEAD 2>/dev/null || cut -c1-7 "$SUB.pin" 2>/dev/null || echo unknown)"
echo "$(cat port/patches/series port/patches/*.patch | sha256sum | cut -c1-16)-$pin" > "$PT/.patchset"
