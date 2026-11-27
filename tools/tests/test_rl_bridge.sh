#!/usr/bin/env bash
# RecompLauncher's achievements (port/kz1hd/rl_bridge.cpp, docs/dev/trophies.md stage 4). Builds the vendored Recomp
# Achievements SDK (third_party/rl_achievements, unchanged) and checks it with its own test (sdk/tests/fake_launcher.py
# and test_channel.c), then the bridge with test_rl_bridge.cpp: under tools/tests/rl_launcher.py (a Unix socket in a
# temporary folder, RL_SESSION set) and without RL_SESSION. Needs the patched tree (tools/ps3recomp-patched.sh) for
# sceNpTrophy.h and cellSaveData.h; no game data, no network.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
grep -q ps3_savedata_loads "$P/libs/system/cellSaveData.h" 2>/dev/null ||
    { echo "FAIL: no ps3_savedata_loads in $P/libs/system/cellSaveData.h; run tools/ps3recomp-patched.sh" >&2; exit 1; }
SDK="$ROOT/third_party/rl_achievements/sdk"
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT

# the SDK as port/kz1hd/CMakeLists.txt builds it (its own CMakeLists.txt): one object per source
objs=()
for c in "$SDK/src/rl_achievements.c" "$SDK/third_party/cjson/cJSON.c" "$SDK"/third_party/rcheevos/src/runtime/*.c \
         "$SDK"/third_party/rcheevos/src/util/*.c; do
    o="$out/$(basename "${c%.c}").o"
    "${CC:-cc}" -std=gnu11 -O1 -w -DRC_DISABLE_LUA -I "$SDK/include" -I "$SDK/third_party/rcheevos/include" \
        -I "$SDK/third_party/rcheevos/src" -I "$SDK/third_party/cjson" -c "$c" -o "$o"
    objs+=("$o")
done

"${CC:-cc}" -std=gnu11 -O1 -I "$SDK/include" "$SDK/tests/test_channel.c" "${objs[@]}" -pthread -lm -o "$out/test_channel"
python3 "$SDK/tests/fake_launcher.py" "$out/test_channel" | tail -1 | grep -qx PASS ||
    { echo "FAIL: the SDK's own channel test" >&2; python3 "$SDK/tests/fake_launcher.py" "$out/test_channel" >&2; exit 1; }
echo "ok   the SDK's own channel test (fake_launcher.py)"

printf '#define VEKTA_VER_STR "9.9.9"\n' > "$out/kz_version.h"
"${CXX:-c++}" -std=c++17 -O1 -UNDEBUG -Wall -Wextra -Werror -DKZ_RL_ACH=1 -I "$ROOT/port/kz1hd" -I "$out" \
    -I "$SDK/include" -I "$P/include" -I "$P/libs/network" -I "$P/libs/system" \
    "$ROOT/port/kz1hd/rl_bridge.cpp" "$ROOT/tools/tests/test_rl_bridge.cpp" "${objs[@]}" -pthread -lm -o "$out/t"
timeout 60 python3 "$ROOT/tools/tests/rl_launcher.py" "$out/t"
env -u RL_SESSION timeout 30 "$out/t" none | grep -qx 'quiet=0' || { echo "FAIL: without RL_SESSION" >&2; exit 1; }
echo "ok   without RL_SESSION: nothing sent, the notice stays"

# built without the SDK (KZ_RL_ACHIEVEMENTS=OFF): the same calls, all doing nothing
"${CXX:-c++}" -std=c++17 -O1 -UNDEBUG -Wall -Wextra -Werror -I "$ROOT/port/kz1hd" -I "$P/include" -I "$P/libs/network" \
    -I "$P/libs/system" "$ROOT/port/kz1hd/rl_bridge.cpp" "$ROOT/tools/tests/test_rl_bridge.cpp" -o "$out/t_off"
RL_SESSION="$out/none.sock" timeout 30 "$out/t_off" none | grep -qx 'quiet=0' || { echo "FAIL: KZ_RL_ACHIEVEMENTS=OFF" >&2; exit 1; }
echo "ok   built without the SDK: nothing sent"
