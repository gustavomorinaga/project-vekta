#!/usr/bin/env python3
"""Host test for how NVIDIA DLSS reaches a build, and that it never reaches a release: the DLSS SDK file set in
tools/release/toolchain.json (the v310.9.16 pins, the files the code includes, consent and licence link); fetching it
(refused without the player's acceptance, only the build OS's files, each checked by size and SHA-256, a wrong
download deleted, --license-only, --offline); firstrun.py's choice (work/dlss-choice, the dlss step and KZ_DLSS_SDK
only when accepted, on Linux and Windows alike); kzbuild.py putting the runtime library beside the program only with
DLSS (libnvidia-ngx-dlss.so beside KZ1HD, nvngx_dlss.dll beside KZ1HD.exe); and assemble_payload.py and check-tree.sh
refusing NVIDIA files by name, folder and content. No network: the downloads
come from a stand-in opener."""
import hashlib
import io
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
REL = ROOT / "tools" / "release"
sys.path.insert(0, str(REL))
sys.path.insert(0, str(ROOT / "tools"))
import assemble_payload as ap  # noqa: E402
import fetch_toolchain as ft  # noqa: E402
import firstrun as fr  # noqa: E402
import kzbuild as kb  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


class Opener:
    def __init__(self, files):
        self.files, self.calls = files, []

    def __call__(self, url):
        self.calls.append(url)
        return io.BytesIO(self.files[url])


# --- the manifest's DLSS SDK set
m = ft.load()
check(ft.validate(m, strict=True) == [], f"the manifest is sound: {ft.validate(m, strict=True)}")
sets = {s["name"]: s for s in m.get("file_sets", [])}
dl = sets.get("dlss-sdk")
check(dl is not None, "toolchain.json has the dlss-sdk file set")
if dl:
    paths = {f["path"]: f for f in dl["files"]}
    check(dl["version"] == "310.9.16" and dl["url"] == "https://raw.githubusercontent.com/NVIDIA/DLSS/v{version}/{path}",
          "the set is pinned to NVIDIA's repository at v310.9.16")
    check("consent" in dl and dl["license_url"].format(version=dl["version"]) == fr.DLSS_LICENSE_URL,
          "the set needs consent, and firstrun shows the same licence link")
    check(paths.get("LICENSE.txt", {}).get("sha256") == "d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0"
          and dl.get("license_file") == "LICENSE.txt", "the licence text is pinned and named")
    so = paths.get("lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.310.9.16", {})
    check(so.get("os") == "linux" and so.get("size") == 59481944 and
          so.get("sha256") == "30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6",
          "the Linux runtime library is pinned by size and hash")
    check(paths.get("lib/Linux_x86_64/libnvsdk_ngx.a", {}).get("os") == "linux", "the Linux NGX library is pinned")
    check(all(f["os"] == "windows" for p, f in paths.items() if p.startswith("lib/Windows")),
          "the Windows files are fetched only for Windows")
    # what kzbuild.py links and puts beside the program is in the set, for its OS
    for target, runtime in (("linux", "libnvidia-ngx-dlss.so.310.9.16"), ("windows", "nvngx_dlss.dll")):
        lib, rel, _ = kb.DLSS_FILES[target]
        for p in ("/".join(lib), "/".join(rel) + "/" + runtime):
            check(paths.get(p, {}).get("os") == target, f"the {target} build's {p} is pinned")
    # every NGX header the DLSS path includes, and what those include, is in the set
    want = {"include/nvsdk_ngx_vk.h", "include/nvsdk_ngx_helpers.h", "include/nvsdk_ngx_helpers_vk.h",
            "include/nvsdk_ngx.h", "include/nvsdk_ngx_defs.h", "include/nvsdk_ngx_defs_vk.h",
            "include/nvsdk_ngx_params.h", "include/nvsdk_ngx_helpers_d3d.h", "include/nvsdk_ngx_helpers_cuda.h"}
    check(want <= {p for p, f in paths.items() if f["os"] == "any"}, f"headers missing: {sorted(want - set(paths))}")

# --- fetching a consent set (a small stand-in with the same shape)
lic, hdr, so_l, dll = b"licence text\n", b"#pragma once\n", b"\x7fELFlinux", b"MZwindows"
fake = {"name": "dlss-sdk", "version": "9.9", "pin": "confirmed", "release_page": "https://example.org/",
        "url": "https://example.org/v{version}/{path}", "license": "x", "consent": "dlss",
        "license_url": "https://example.org/LICENSE", "license_file": "LICENSE.txt",
        "files": [{"path": p, "os": o, "size": len(d), "sha256": hashlib.sha256(d).hexdigest()}
                  for p, o, d in (("LICENSE.txt", "any", lic), ("include/a.h", "any", hdr),
                                  ("lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.9.9", "linux", so_l),
                                  ("lib/Windows_x86_64/rel/nvngx_dlss.dll", "windows", dll))]}
urls = {f"https://example.org/v9.9/{f['path']}": d for f, d in zip(fake["files"], (lic, hdr, so_l, dll))}
check(ft.validate({"format": 1, "components": [], "file_sets": [fake]}) == [], "the stand-in set validates")
bad = dict(fake, files=[dict(fake["files"][0], path="../x")])
check(any("bad or repeated path" in p for p in ft.validate({"format": 1, "components": [], "file_sets": [bad]})),
      "a path leaving the set's folder is refused")
with tempfile.TemporaryDirectory() as tmp:
    cache = os.path.join(tmp, "cache")
    op = Opener(urls)
    try:
        ft.fetch_set(fake, cache, "linux", accepted=False, opener=op, out=io.StringIO())
        check(False, "a consent set is refused without --accept-license")
    except ValueError as err:
        check("licence must be accepted" in str(err) and not op.calls, "refused without acceptance, nothing fetched")
    p = ft.fetch_set(fake, cache, "linux", license_only=True, opener=op, out=io.StringIO())
    check(p.endswith("LICENSE.txt") and open(p, "rb").read() == lic and len(op.calls) == 1,
          "--license-only fetches the licence text alone, without acceptance")
    tree = ft.fetch_set(fake, cache, "linux", accepted=True, opener=op, out=io.StringIO())
    got = sorted(os.path.relpath(os.path.join(d, f), tree) for d, _, fs in os.walk(tree) for f in fs)
    check(got == ["LICENSE.txt", "include/a.h", "lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.9.9"],
          f"only the Linux and common files, laid out as the SDK: {got}")
    n = len(op.calls)
    ft.fetch_set(fake, cache, "linux", accepted=True, offline=True, opener=op, out=io.StringIO())
    check(len(op.calls) == n, "files already there with their pins are kept, also --offline")
    os.remove(os.path.join(tree, "include", "a.h"))
    try:
        ft.fetch_set(fake, cache, "linux", accepted=True, offline=True, opener=op, out=io.StringIO())
        check(False, "--offline refuses a missing file")
    except ValueError:
        pass
    op2 = Opener(dict(urls, **{"https://example.org/v9.9/include/a.h": b"tampered"}))
    try:
        ft.fetch_set(fake, cache, "linux", accepted=True, opener=op2, out=io.StringIO())
        check(False, "a download that is not the pin is refused")
    except ValueError as err:
        check("the pin is" in str(err) and not os.path.exists(os.path.join(tree, "include", "a.h")),
              "the wrong download is deleted")

# --- firstrun's choice
with tempfile.TemporaryDirectory() as tmp:
    c = fr.Ctx(root=str(ROOT), paths={}, work=tmp)
    check(fr.read_dlss(tmp) == "" and fr.cmd_dlss(c) == [] and "KZ_DLSS_SDK" not in c.env(),
          "never answered: no dlss step, no KZ_DLSS_SDK")
    fr.write_dlss(tmp, "accept")
    c.dlss = fr.read_dlss(tmp)
    cmds = fr.cmd_dlss(c)
    check(len(cmds) == 1 and "--accept-license" in cmds[0] and "dlss-sdk" in cmds[0],
          "accepted: the step fetches the set with the acceptance (Linux and Windows)")
    check(c.env().get("KZ_DLSS_SDK") == os.path.join(tmp, "toolchain-cache", "dlss-sdk"),
          "accepted: the compile gets KZ_DLSS_SDK")
    fr.write_dlss(tmp, "decline")
    c.dlss = fr.read_dlss(tmp)
    os.environ["KZ_DLSS_SDK"] = "/somewhere"
    check(fr.cmd_dlss(c) == [] and "KZ_DLSS_SDK" not in c.env(), "declined: no download, KZ_DLSS_SDK dropped")
    del os.environ["KZ_DLSS_SDK"]
    names = [s.name for s in fr.STEPS]
    check(names.index("dlss") == names.index("compile") - 1, "the dlss step comes right before compile")

# --- kzbuild: the runtime library beside the program only with DLSS
with tempfile.TemporaryDirectory() as tmp:
    sdk, b = os.path.join(tmp, "sdk"), os.path.join(tmp, "build")
    os.makedirs(os.path.join(sdk, "lib", "Linux_x86_64", "rel"))
    os.makedirs(b)
    open(os.path.join(sdk, "lib", "Linux_x86_64", "libnvsdk_ngx.a"), "wb").close()
    open(os.path.join(sdk, "lib", "Linux_x86_64", "rel", "libnvidia-ngx-dlss.so.9.9"), "wb").write(b"so")
    open(os.path.join(b, "KZ1HD"), "wb").close()
    check(kb.dlss_defs({"KZ_DLSS_SDK": sdk}) == [f"-DPS3RECOMP_DLSS_SDK={sdk}"] and
          kb.dlss_defs({}) == ["-DPS3RECOMP_DLSS_SDK="], "KZ_DLSS_SDK becomes the CMake option (empty when unset)")
    try:
        kb.dlss_defs({"KZ_DLSS_SDK": tmp})
        check(False, "a folder that is not the SDK is refused")
    except kb.Fail:
        pass
    kb.place_dlss({"KZ_DLSS_SDK": sdk}, b)
    check(os.path.isfile(os.path.join(b, "libnvidia-ngx-dlss.so.9.9")), "with DLSS the runtime goes beside KZ1HD")
    kb.place_dlss({}, b)
    check(sorted(os.listdir(b)) == ["KZ1HD"], "without DLSS no NVIDIA file is left beside KZ1HD")
    # the Windows exe: the MSVC NGX library to link, nvngx_dlss.dll beside KZ1HD.exe
    bw = os.path.join(tmp, "build-win")
    os.makedirs(os.path.join(sdk, "lib", "Windows_x86_64", "x64"))
    os.makedirs(os.path.join(sdk, "lib", "Windows_x86_64", "rel"))
    os.makedirs(bw)
    try:
        kb.dlss_defs({"KZ_DLSS_SDK": sdk}, "windows")
        check(False, "the Windows build refuses an SDK without nvsdk_ngx_s.lib")
    except kb.Fail as err:
        check("nvsdk_ngx_s.lib" in str(err), str(err))
    open(os.path.join(sdk, "lib", "Windows_x86_64", "x64", "nvsdk_ngx_s.lib"), "wb").close()
    for n in ("nvngx_dlss.dll", "nvngx_dlssg.dll"):
        open(os.path.join(sdk, "lib", "Windows_x86_64", "rel", n), "wb").write(b"MZ")
    open(os.path.join(bw, "KZ1HD.exe"), "wb").close()
    check(kb.dlss_defs({"KZ_DLSS_SDK": sdk}, "windows") == [f"-DPS3RECOMP_DLSS_SDK={sdk}"],
          "the Windows build takes the SDK with nvsdk_ngx_s.lib")
    kb.place_dlss({"KZ_DLSS_SDK": sdk}, bw, "windows")
    check(sorted(os.listdir(bw)) == ["KZ1HD.exe", "nvngx_dlss.dll"], f"with DLSS nvngx_dlss.dll alone goes beside "
          f"KZ1HD.exe: {sorted(os.listdir(bw))}")
    kb.place_dlss({}, bw, "windows")
    check(sorted(os.listdir(bw)) == ["KZ1HD.exe"], "without DLSS no NVIDIA file is left beside KZ1HD.exe")

# --- no NVIDIA file in a release, nor in the repository
pins = ap.nvidia_pins(m)
check(len(pins) == len(dl["files"]) if dl else False, "every pinned DLSS file is known to the payload check")
with tempfile.TemporaryDirectory() as tmp:
    plain = os.path.join(tmp, "plain")
    open(plain, "wb").write(lic)
    check(ap.nvidia_problem("work/ps3recomp-patched/libs/video/rsx_vk_dlss.cpp", plain, pins) is None,
          "the port's own DLSS source is not NVIDIA's")
    for rel in ("nvngx_dlss.dll", "lib/libnvidia-ngx-dlss.so.310.9.16", "include/nvsdk_ngx.h",
                "work/toolchain-cache/dlss-sdk/README.txt"):
        check(ap.nvidia_problem(rel, plain, pins) is not None, f"refused by name or folder: {rel}")
    renamed = os.path.join(tmp, "renamed.bin")
    open(renamed, "wb").write(lic)
    check(ap.nvidia_problem("x/renamed.bin", renamed, {(len(lic), hashlib.sha256(lic).hexdigest())}) is not None,
          "a pinned NVIDIA file is refused under any name")
    repo = os.path.join(tmp, "repo")
    os.makedirs(os.path.join(repo, "tools"))
    for f in ("check-tree.sh",):
        open(os.path.join(repo, "tools", f), "w").write(open(ROOT / "tools" / f).read())
    open(os.path.join(repo, "nvngx_dlss.dll"), "wb").write(b"MZ")
    subprocess.run(["git", "init", "-q", repo], check=True)
    r = subprocess.run(["sh", os.path.join(repo, "tools", "check-tree.sh")], capture_output=True, text=True)
    check(r.returncode == 1 and "NVIDIA DLSS SDK file" in r.stdout, f"check-tree refuses an NVIDIA file: {r.stdout}")

print("test_dlss_distribution:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
