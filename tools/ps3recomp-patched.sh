#!/usr/bin/env bash
# Make $PS3RECOMP_DIR (default work/ps3recomp-patched): a fresh copy of third_party/ps3recomp at its pinned commit
# with every patch in port/patches/series applied, in that order. The lift (tools/lift.sh) and the builds use it,
# so the submodule itself stays clean. Only the files whose content changed are rewritten, so the build recompiles
# only what the patch set changed; $PS3RECOMP_DIR/.patchset names the patch set the tree was made from.
# The step itself is `tools/kzbuild.py patch` (the same code on Windows), adapted from Project Helghast's
# tools/ps3recomp-patched.sh (undecuple, MIT); this finds a Python 3 and runs it, as theirs does.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
for c in "python3" "python" "py -3"; do
  read -r -a cand <<<"$c"
  if command -v "${cand[0]}" >/dev/null 2>&1 &&
     "${cand[@]}" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' >/dev/null 2>&1; then
    exec "${cand[@]}" "$here/kzbuild.py" patch "$@"
  fi
done
echo "ps3recomp-patched: no Python 3.8 or newer found (python3, python, py -3)" >&2
exit 1
