#!/usr/bin/env bash
# Builds and runs test_spu_put_recheck.c against the patched tree (tools/ps3recomp-patched.sh first).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"; S="$P/runtime/spu"
[[ -f "$S/spu_channels.c" ]] || { echo "FAIL: no patched tree at $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
units=(spu_drain spu_lockstep spu_coherency spu_workload spurs_policy spurs_job spu_tsp_weak spu_vm_pagemap
       spurs_policy_blob_weak spu_interp)
srcs=(); for u in "${units[@]}"; do srcs+=("$S/$u.c"); done
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -w -I "$P/include" -I "$S" -I "$P/runtime/platform" \
  "$ROOT/tools/tests/test_spu_put_recheck.c" "${srcs[@]}" \
  "$P/runtime/platform/win32_compat.c" -pthread -lm -o "$out/t"
timeout 120 "$out/t"
