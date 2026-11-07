#!/usr/bin/env bash
# Patch 2056: builds and runs test_vk_cache_key.c against the patched tree (tools/ps3recomp-patched.sh first), then
# checks with `cmake -P` that cmake/RsxSpirvTranslatorId.cmake gives the same translator id for the same files and
# another one when the translator's source or the glslang version changes.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/rsx_vk_cache_key.h" ]] || { echo "FAIL: no rsx_vk_cache_key.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_vk_cache_key.c" -o "$out/t"
timeout 60 "$out/t"

command -v cmake >/dev/null || { echo "FAIL: cmake not found" >&2; exit 1; }
printf 'int translate(void) { return 1; }\n' >"$out/a.cpp"
printf '#define BINDING 16u\n' >"$out/a.h"
cat >"$out/id.cmake" <<CM
include("$P/cmake/RsxSpirvTranslatorId.cmake")
rsx_spirv_translator_id(id "$out/a.cpp" "$out/a.h")
message("\${id}")
CM
id() { cmake -Dglslang_VERSION="$1" -P "$out/id.cmake" 2>&1; }
a=$(id 15.0.0); b=$(id 15.0.0)
[[ "$a" =~ ^[0-9a-f]{16}$ ]] || { echo "FAIL: translator id '$a' is not 16 hex digits" >&2; exit 1; }
[[ "$a" == "$b" ]] || { echo "FAIL: the same files gave two ids ($a, $b)" >&2; exit 1; }
[[ "$(id 15.1.0)" != "$a" ]] || { echo "FAIL: another glslang, the same id" >&2; exit 1; }
printf 'int translate(void) { return 2; }\n' >"$out/a.cpp"
[[ "$(id 15.0.0)" != "$a" ]] || { echo "FAIL: another translator source, the same id" >&2; exit 1; }
echo "test_vk_cache_key (translator id): ok"
