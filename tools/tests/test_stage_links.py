#!/usr/bin/env python3
"""Host test for fetch_toolchain.place, which the deps scripts and tools/kzbuild.py stage FFmpeg's libraries and the
Vulkan headers with: a hard link to the cache's file where the file system makes one (no second and third copy of
120 MB), a copy where it does not; a file that is already there is replaced and never written in place (it may be a
link to another file); a copy that is current and cannot be replaced (in use) stays; the DLSS library, which players
swap, stays a copy. Runs on Windows too (tools/host-tests-windows.txt): NTFS hard links, os.replace over a file."""
import errno
import os
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
sys.path.insert(0, str(ROOT / "tools"))
import fetch_toolchain as ft  # noqa: E402
import kzbuild  # noqa: E402

fails = 0
T0 = 1_700_000_000


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def write(path, data, when):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    os.utime(path, (when, when))


def leftovers(folder):
    return sorted(p.name for p in folder.iterdir() if p.name.endswith(".new"))


with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    src, dst = tmp / "cache" / "libav.so.63.1.102", tmp / "deps" / "libav.so.63"
    write(src, b"library one", T0)
    dst.parent.mkdir()
    try:
        os.link(src, tmp / "probe")
        links = True
    except OSError:
        links = False
        print("note: no hard links in the temporary folder: only the copy paths are checked")
    real_link, real_replace = os.link, os.replace

    def no_link(*a, **k):
        raise OSError(errno.EXDEV, "Invalid cross-device link")

    def no_replace(*a, **k):
        raise PermissionError(errno.EACCES, "The process cannot access the file because it is being used")

    if links:
        check(ft.place(str(src), str(dst)) == "link" and os.path.samefile(src, dst), "a hard link to the cache's file")
        check(ft.place(str(src), str(dst)) == "kept" and not leftovers(dst.parent), "placed again: nothing to do")
        # a copy from before this (an install made by an older version) becomes a link
        dst.unlink()
        write(dst, b"library one", T0)
        check(ft.place(str(src), str(dst)) == "link" and os.path.samefile(src, dst), "an older copy becomes a link")
        # dst is a link to another file: that file is not written to
        other = tmp / "other"
        write(other, b"another file", T0 + 50)
        dst.unlink()
        os.link(other, dst)
        check(ft.place(str(src), str(dst)) == "link" and other.read_bytes() == b"another file" and
              dst.read_bytes() == b"library one", "a dst that is a link to another file is replaced, not written in place")
        # a file of its own at dst that is newer than src (left by a newer pin, or put there by hand) gives way too
        dst.unlink()
        write(dst, b"library from a newer pin", T0 + 500)
        check(ft.place(str(src), str(dst)) == "link" and os.path.samefile(src, dst) and
              dst.read_bytes() == b"library one", "with a link, a newer file of its own at dst becomes the pinned file")
        check(kzbuild.place(str(src), str(tmp / "deps" / "beside-the-program")) == "link",
              "kzbuild.place is fetch_toolchain.place")
        dst.unlink()

    # no hard links (another drive, exFAT): a copy, made when dst is missing or older
    os.link = no_link
    try:
        check(ft.place(str(src), str(dst)) == "copy" and dst.read_bytes() == b"library one" and
              not os.path.samefile(src, dst) and dst.stat().st_mtime == src.stat().st_mtime,
              "where no link can be made: a copy with the source's time")
        check(ft.place(str(src), str(dst)) == "kept", "a copy that is not older is kept")
        write(src, b"library two", T0 + 100)
        if links:
            real_link(dst, tmp / "keep")
        check(ft.place(str(src), str(dst)) == "copy" and dst.read_bytes() == b"library two" and
              (not links or (tmp / "keep").read_bytes() == b"library one") and not leftovers(dst.parent),
              "a newer source is copied again, and the old file is replaced, not written in place")
    finally:
        os.link = real_link

    # dst cannot be replaced (the game is running, on Windows): a current copy stays, an old one is an error
    os.replace = no_replace
    try:
        check(ft.place(str(src), str(dst)) == "kept" and dst.read_bytes() == b"library two" and
              not leftovers(dst.parent), "a current copy that is in use stays, and no .new file is left")
        write(src, b"library three", T0 + 200)
        try:
            ft.place(str(src), str(dst))
            check(False, "an old copy that cannot be replaced is an error")
        except PermissionError:
            check(dst.read_bytes() == b"library two" and not leftovers(dst.parent),
                  "after the error the old copy is whole and no .new file is left")
    finally:
        os.replace = real_replace
    write(dst.parent / "libav.so.63.new", b"left by a killed run", T0)
    check(ft.place(str(src), str(dst)) in ("link", "copy") and dst.read_bytes() == b"library three" and
          not leftovers(dst.parent), "a .new file left by a killed run is no obstacle")

    # the DLSS library stays a copy: writing over it must not reach the SDK's file, which is checked against its pin
    sdk, build = tmp / "dlss-sdk", tmp / "build"
    write(sdk / "lib" / "Linux_x86_64" / "rel" / "libnvidia-ngx-dlss.so.310.4.0", b"the SDK's library", T0)
    build.mkdir()
    kzbuild.place_dlss({"KZ_DLSS_SDK": str(sdk)}, str(build))
    placed = build / "libnvidia-ngx-dlss.so.310.4.0"
    check(placed.read_bytes() == b"the SDK's library" and
          not os.path.samefile(placed, sdk / "lib" / "Linux_x86_64" / "rel" / placed.name),
          "the DLSS library beside the program is a copy, not a link")

# the two deps scripts and the build stage with it
for script, calls in (("tools/release/build_linux_deps.py", ('fetch_toolchain.place(c("ffmpeg-linux", "lib", f)',
                                                             '"include", name),\n' + " " * 25 + "fetch_toolchain.place)")),
                      ("tools/release/build_win_deps.py", ('fetch_toolchain.place(c("ffmpeg", "bin", f)',
                                                           '"include", name),\n' + " " * 25 + "fetch_toolchain.place)")),
                      ("tools/kzbuild.py", ("            place(src, dst)\n", "            place(j(dll_dir, d), j(b, d))\n"))):
    text = (ROOT / script).read_text(encoding="utf-8")
    for call in calls:
        check(call in text, f"{script} stages with place: {call.strip()!r}")

print("test_stage_links:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
