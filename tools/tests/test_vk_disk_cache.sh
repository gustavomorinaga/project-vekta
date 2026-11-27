#!/usr/bin/env bash
# The Vulkan shader and pipeline caches on Windows (patch 2099), on the patched tree (tools/ps3recomp-patched.sh
# first; no GPU). Windows sets no HOME, and the backend took its cache folder from $PS3RECOMP_VK_CACHE or HOME
# only, so on Windows the SPIR-V files, pipelines.bin, the pipeline list built ahead (2044) and the MLAA marker
# (2087) were all off: a tester's log had no "shader cache: translator" line. Checks: test_vk_disk_cache.c (the
# folder pick, a replace over an existing file, the pipelines.bin header check); the backend uses them and logs the
# folder; the port gives Windows its own folder (work/vk-cache) and the uninstaller removes it.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KZ_ROOT="$ROOT"; . "$ROOT/tools/env.sh"
P="$PS3RECOMP_DIR"
[[ -f "$P/libs/video/rsx_vk_disk_cache.h" ]] || { echo "FAIL: no rsx_vk_disk_cache.h in $P; run tools/ps3recomp-patched.sh" >&2; exit 1; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
"${CC:-cc}" -std=gnu17 -O1 -UNDEBUG -Wall -Werror -I "$P/libs/video" "$ROOT/tools/tests/test_vk_disk_cache.c" -o "$out/t"
timeout 60 "$out/t" "$out"
python3 - "$P/libs/video/rsx_vulkan_backend.c" "$ROOT/port/kz1hd/main.cpp" "$ROOT/installer/vekta.iss" <<'PY'
import re, sys
be, main, iss = (open(p, encoding='utf-8').read() for p in sys.argv[1:])
fails = []
m = re.search(r'static const char\* vk_cache_dir\(void\)\n\{(.*?)\n\}\n', be, re.S)
if not m: fails.append('vk_cache_dir not found')
else:
    body = m.group(1)
    if 'rsx_vk_cache_dir_pick(' not in body or 'getenv("LOCALAPPDATA")' not in body:
        fails.append('vk_cache_dir does not pick the folder with LOCALAPPDATA on Windows')
    if 'VK_LOG("shader cache: %s\\n", dir)' not in body: fails.append('vk_cache_dir does not log its folder')
m = re.search(r'static void vk_cache_write\(.*?\n\{(.*?)\n\}\n', be, re.S)
if not m or 'rsx_vk_replace_file(tmp, path)' not in m.group(1) or 'rename(' in m.group(1):
    fails.append('vk_cache_write still renames (fails on Windows over an existing file)')
m = re.search(r'static VkPipelineCache vk_pipeline_cache\(void\)\n\{(.*?)\n\}\n', be, re.S)
if not m or 'rsx_vk_pcache_header_ok(' not in m.group(1): fails.append('pipelines.bin reaches the driver unchecked')
m = re.search(r'static void set_edat_cache\(\)\n\{(.*?)\n\}\n', main, re.S)
if not m or not re.search(r'#ifdef _WIN32\n(?:.*\n)*?\s*setenv\("PS3RECOMP_VK_CACHE", \(std::string\(ppu_vfs_root\) \+ "/\.\./vk-cache"\)\.c_str\(\), 0\);\n#endif', m.group(1)):
    fails.append('the port does not give Windows work/vk-cache')
if 'Type: filesandordirs; Name: "{app}\\work\\vk-cache"' not in iss: fails.append('the uninstaller leaves work\\vk-cache')
for f in fails: print('FAIL:', f)
print('test_vk_disk_cache (backend, port, installer):', 'FAIL' if fails else 'ok')
sys.exit(1 if fails else 0)
PY
