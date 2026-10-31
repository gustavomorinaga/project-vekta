#!/usr/bin/env python3
"""Host test for tools/release/fetch_toolchain.py and the manifest tools/release/toolchain.json: the manifest is
sound; entries are picked by host; a download is checked against its pin, unpacked (zip, tar.gz, tar.xz, with and
without the top folder) and kept for the next run; a wrong hash is refused and deleted; a TO-FILL hash is refused
unless --fill, which writes the hash into the manifest; --offline never opens a URL; unsafe archive members (an
absolute path, "..", a zip symlink, a tar link out of the tree) are refused; build_win_deps.py names what is not
fetched yet. No network: the downloads come from a stand-in opener."""
import hashlib
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
REL = ROOT / "tools" / "release"
sys.path.insert(0, str(REL))
import fetch_toolchain as ft  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def zip_bytes(members, top="pkg-1.0/"):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        for name, data in members.items():
            z.writestr(top + name, data)
    return b.getvalue()


def tar_bytes(members, mode="w:gz", top="pkg-1.0/", links=()):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode=mode) as t:
        for name, data in members.items():
            info = tarfile.TarInfo(top + name)
            info.size, info.mode = len(data), 0o755 if name.endswith(".sh") else 0o644
            t.addfile(info, io.BytesIO(data))
        for name, target in links:
            info = tarfile.TarInfo(top + name)
            info.type, info.linkname = tarfile.SYMTYPE, target
            t.addfile(info)
    return b.getvalue()


class Opener:
    def __init__(self, files):
        self.files, self.calls = files, []

    def __call__(self, url):
        self.calls.append(url)
        return io.BytesIO(self.files[url])


def entry(name, url, data, archive, sha=None, strip=True, host="any"):
    return {"name": name, "host": host, "version": "1.0", "pin": "confirmed", "release_page": "https://example.org/",
            "url": url, "sha256": sha or hashlib.sha256(data).hexdigest(), "archive": archive, "strip_top": strip,
            "license": "MIT"}


# the real manifest
m = ft.load()
check(ft.validate(m) == [], f"toolchain.json is sound: {ft.validate(m)}")
strict = ft.validate(m, strict=True)
check(all("TO-" in p for p in strict), f"--strict only names what is still to fill or confirm: {strict}")
names = lambda host: {e["name"] for e in ft.entries_for(m, host)}  # noqa: E731
check("llvm-mingw-linux" in names("linux") and "llvm-mingw" not in names("linux"), "linux takes the cross compiler")
check({"llvm-mingw", "cmake", "ninja", "python"} <= names("windows") and "llvm-mingw-linux" not in names("windows"),
      "windows takes the Windows-hosted tools")
check({"sdl2", "zlib", "glslang", "vulkan-headers"} <= names("linux") & names("windows"), "the libraries go to both")
by = {e["name"]: e for e in m["components"]}
check(by["llvm-mingw"]["sha256"] == "317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a",
      "the Windows llvm-mingw pin is Project Pyrrhus's")
check(ft.expand(by["zlib"])["url"].endswith("/v1.3.1/zlib-1.3.1.tar.gz"), "{version} is filled into the url")
bad = json.loads(json.dumps(m))
bad["components"][0]["url"] = "http://x.org/a.zip"
bad["components"][1]["sha256"] = "abc"
bad["components"][2]["host"] = "mac"
check(len(ft.validate(bad)) == 3, f"a plain-http url, a short hash and an unknown host are reported: {ft.validate(bad)}")

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cache = str(tmp / "cache")
    z = zip_bytes({"bin/tool.exe": b"exe", "LICENSE": b"MIT"})
    tg = tar_bytes({"include/a.h": b"h", "run.sh": b"#!/bin/sh"}, links=[("include/b.h", "a.h")])
    tx = tar_bytes({"lib/x.a": b"a"}, mode="w:xz")
    files = {"https://example.org/z.zip": z, "https://example.org/t.tar.gz": tg, "https://example.org/x.tar.xz": tx}
    op = Opener(files)
    out = io.StringIO()
    t1 = ft.fetch_one(entry("zz", "https://example.org/z.zip", z, "zip"), cache, opener=op, out=out)
    check(open(os.path.join(t1, "bin", "tool.exe"), "rb").read() == b"exe", "a zip is unpacked without its top folder")
    t2 = ft.fetch_one(entry("tt", "https://example.org/t.tar.gz", tg, "tar.gz"), cache, opener=op, out=out)
    check(os.readlink(os.path.join(t2, "include", "b.h")) == "a.h" and os.access(os.path.join(t2, "run.sh"), os.X_OK),
          "a tar.gz keeps its links inside the tree and its modes")
    t3 = ft.fetch_one(entry("xx", "https://example.org/x.tar.xz", tx, "tar.xz", strip=False), cache, opener=op, out=out)
    check(os.path.isfile(os.path.join(t3, "pkg-1.0", "lib", "x.a")), "a tar.xz is unpacked, with its top folder")
    n = len(op.calls)
    ft.fetch_one(entry("zz", "https://example.org/z.zip", z, "zip"), cache, opener=op, out=out)
    check(len(op.calls) == n, "an entry already unpacked from its pin is not fetched again")

    files["https://example.org/bad.zip"] = z
    e = entry("bad", "https://example.org/bad.zip", z, "zip", sha="0" * 64)
    try:
        ft.fetch_one(e, cache, opener=op, out=out)
        check(False, "a wrong hash is refused")
    except ValueError as err:
        check("pin is" in str(err) and not os.path.exists(os.path.join(cache, "downloads", "bad.zip")),
              "a wrong hash is refused and its download deleted")
    check(not os.path.exists(os.path.join(cache, "bad")), "nothing is unpacked from a wrong download")

    # TO-FILL: refused, then --fill writes the hash into the manifest
    man = tmp / "toolchain.json"
    fill_e = entry("fillme", "https://example.org/t.tar.gz", tg, "tar.gz", sha=ft.TO_FILL)
    man.write_text(json.dumps({"format": 1, "components": [entry("other", "https://example.org/z.zip", z, "zip",
                                                                  sha=ft.TO_FILL), fill_e]}, indent=1) + "\n")
    try:
        ft.fetch_one(fill_e, cache, manifest_path=str(man), opener=op, out=out)
        check(False, "TO-FILL is refused without --fill")
    except ValueError:
        pass
    ft.fetch_one(fill_e, cache, fill=True, manifest_path=str(man), opener=op, out=out)
    got = {c["name"]: c["sha256"] for c in json.loads(man.read_text())["components"]}
    check(got == {"other": ft.TO_FILL, "fillme": hashlib.sha256(tg).hexdigest()},
          f"--fill writes the hash of that entry only: {got}")

    # --offline: the download in the cache with the pinned hash, never the network
    shutil.rmtree(os.path.join(cache, "zz"))
    n = len(op.calls)
    ft.fetch_one(entry("zz", "https://example.org/z.zip", z, "zip"), cache, offline=True, opener=op, out=out)
    check(len(op.calls) == n and os.path.isdir(os.path.join(cache, "zz")), "--offline unpacks the cached download")
    try:
        ft.fetch_one(entry("nope", "https://example.org/none.zip", b"x", "zip"), cache, offline=True, opener=op, out=out)
        check(False, "--offline without the download is refused")
    except ValueError:
        pass

    # unsafe members
    for what, data, kind in [
            ("an absolute path", zip_bytes({"x": b"x"}, top="/abs/"), "zip"),
            ("a '..' path", zip_bytes({"../../evil": b"x"}, top=""), "zip"),
            ("a tar link out of the tree", tar_bytes({"a": b"a"}, links=[("l", "../../../etc/passwd")]), "tar.gz")]:
        p = tmp / f"u.{kind}"
        p.write_bytes(data)
        try:
            ft.unpack(str(p), kind, str(tmp / ("out-" + what.replace(" ", "-"))), True)
            check(False, f"{what} is refused")
        except ValueError:
            pass
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as zf:
        info = zipfile.ZipInfo("pkg/link")
        info.external_attr = (0o120777) << 16
        zf.writestr(info, "target")
    (tmp / "s.zip").write_bytes(b.getvalue())
    try:
        ft.unpack(str(tmp / "s.zip"), "zip", str(tmp / "out-sym"), True)
        check(False, "a zip symlink is refused")
    except ValueError:
        pass

    # build_win_deps names what is missing
    r = subprocess.run([sys.executable, str(REL / "build_win_deps.py"), "--cache", str(tmp / "empty"),
                        "--prefix", str(tmp / "deps")], capture_output=True, text=True)
    check(r.returncode == 1 and "llvm-mingw-linux" in r.stderr and "glslang" in r.stderr,
          "build_win_deps names what is not fetched: " + r.stderr)

r = subprocess.run([sys.executable, str(REL / "fetch_toolchain.py"), "--check"], capture_output=True, text=True)
check(r.returncode == 0 and "manifest ok" in r.stdout, "--check passes on the real manifest")

print("test_toolchain:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
