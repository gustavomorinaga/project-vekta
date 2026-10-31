#!/usr/bin/env python3
"""Build and stage the Windows build's libraries into one prefix (default work/win-deps), from the pinned sources
and packages tools/release/fetch_toolchain.py unpacked, with the pinned llvm-mingw:

    tools/release/build_win_deps.py [--cache DIR] [--prefix DIR] [--llvm-mingw DIR] [--jobs N]

  SDL2            the MinGW package's x86_64 tree, copied (headers, import library, SDL2.dll, its CMake package)
  zlib            built from source, static (lib/libz.a; no DLL)
  glslang         built from source, static, HLSL front end on, no optimizer, no tools: ps3recomp compiles the guest
                  shaders with its C++ API, which must be built with the exe's own libc++
  Vulkan headers  include/vulkan, include/vk_video
  FFmpeg          include/lib* (the headers the movie loader compiles against) and bin/*.dll (loaded at run time),
                  when the ffmpeg entry has been fetched

The prefix's stamp.json records the pins it was made from; a run with the same pins and llvm-mingw does nothing.
CMake and Ninja: the ones on PATH (work/venv on Linux). Exit 0 = staged, 1 = a step failed. Standard library only.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import fetch_toolchain  # noqa: E402

TOOLCHAIN_FILE = os.path.join(ROOT, "port", "cmake", "llvm-mingw.cmake")
PARTS = ("sdl2", "zlib", "glslang", "vulkan-headers", "ffmpeg")


def copytree(src, dst):
    for d, _, files in os.walk(src):
        rel = os.path.relpath(d, src)
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in files:
            shutil.copy2(os.path.join(d, f), os.path.join(dst, rel, f))


def run(argv, log):
    print("$ " + " ".join(argv), file=log, flush=True)
    r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write(r.stdout)
    if r.returncode:
        tail = "\n".join(r.stdout.strip().splitlines()[-15:])
        raise RuntimeError(f"{argv[0]} failed (exit {r.returncode}):\n{tail}")


def cmake_build(name, src, build, prefix, llvm_mingw, jobs, options, log):
    if os.path.isdir(build):
        shutil.rmtree(build)
    run(["cmake", "-G", "Ninja", "-S", src, "-B", build, f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN_FILE}",
         f"-DLLVM_MINGW_ROOT={llvm_mingw}", f"-DKZ_WIN_DEPS={prefix}", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_INSTALL_PREFIX={prefix}", *options], log)
    run(["cmake", "--build", build, "-j", str(jobs)], log)
    run(["cmake", "--install", build], log)
    print(f"{name}: built", flush=True)


def stamp_of(m, llvm_mingw):
    pins = {e["name"]: e["sha256"] for e in m["components"] if e["name"] in PARTS}
    return {"pins": pins, "llvm_mingw": os.path.realpath(llvm_mingw), "toolchain_file":
            fetch_toolchain.sha256_file(TOOLCHAIN_FILE)}


def main(argv=None):
    work = os.environ.get("KZ_WORK") or os.path.join(ROOT, "work")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cache", default=os.path.join(work, "toolchain-cache"))
    ap.add_argument("--prefix", default=os.path.join(work, "win-deps"))
    ap.add_argument("--llvm-mingw", help="default: <cache>/llvm-mingw-linux (llvm-mingw on Windows)")
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("KZ_JOBS") or os.cpu_count() or 4))
    a = ap.parse_args(argv)
    c = lambda *p: os.path.join(a.cache, *p)  # noqa: E731
    llvm = a.llvm_mingw or c("llvm-mingw" if os.name == "nt" else "llvm-mingw-linux")
    need = [n for n in ("sdl2", "zlib", "glslang", "vulkan-headers") if not os.path.isdir(c(n))]
    if not os.path.isdir(os.path.join(llvm, "bin")):
        need.insert(0, os.path.basename(llvm))
    if need:
        print(f"build_win_deps: not fetched yet: {', '.join(need)} (tools/release/fetch_toolchain.py)", file=sys.stderr)
        return 1
    m = fetch_toolchain.load()
    want = stamp_of(m, llvm)
    stamp = os.path.join(a.prefix, "stamp.json")
    try:
        with open(stamp, encoding="utf-8") as f:
            if json.load(f) == want:
                print(f"build_win_deps: {a.prefix} is current")
                return 0
    except (OSError, ValueError):
        pass
    if os.path.isdir(a.prefix):
        shutil.rmtree(a.prefix)
    os.makedirs(a.prefix)
    tmp = os.path.join(os.path.dirname(a.prefix), "win-deps-build")
    log_path = os.path.join(os.path.dirname(a.prefix), "win-deps.log")
    try:
        with open(log_path, "w", encoding="utf-8") as log:
            copytree(c("sdl2", "x86_64-w64-mingw32"), a.prefix)
            # SDL2 stays a DLL: without its static archive, the exe's -static link cannot pick it by mistake
            # (it would need a dozen more system libraries and lose the DLL's own updates).
            for f in ("libSDL2.a", "libSDL2.la", "libSDL2_test.a", "libSDL2_test.la"):
                os.remove(os.path.join(a.prefix, "lib", f))
            print("sdl2: staged", flush=True)
            for name in ("vk_video", "vulkan"):
                copytree(c("vulkan-headers", "include", name), os.path.join(a.prefix, "include", name))
            print("vulkan-headers: staged", flush=True)
            cmake_build("zlib", c("zlib"), os.path.join(tmp, "zlib"), a.prefix, llvm, a.jobs,
                        ["-DZLIB_BUILD_EXAMPLES=OFF"], log)
            # zlib linked statically: only libzlibstatic.a, under the name find_package(ZLIB) looks for.
            os.remove(os.path.join(a.prefix, "lib", "libzlib.dll.a"))
            os.remove(os.path.join(a.prefix, "bin", "libzlib.dll"))
            os.replace(os.path.join(a.prefix, "lib", "libzlibstatic.a"), os.path.join(a.prefix, "lib", "libz.a"))
            cmake_build("glslang", c("glslang"), os.path.join(tmp, "glslang"), a.prefix, llvm, a.jobs,
                        ["-DBUILD_SHARED_LIBS=OFF", "-DENABLE_OPT=OFF", "-DENABLE_HLSL=ON", "-DGLSLANG_TESTS=OFF",
                         "-DENABLE_GLSLANG_BINARIES=OFF", "-DBUILD_EXTERNAL=OFF", "-DENABLE_SPVREMAPPER=OFF"], log)
            if os.path.isdir(c("ffmpeg")):
                for d in os.listdir(c("ffmpeg", "include")):
                    copytree(c("ffmpeg", "include", d), os.path.join(a.prefix, "include", d))
                os.makedirs(os.path.join(a.prefix, "bin"), exist_ok=True)
                for f in os.listdir(c("ffmpeg", "bin")):
                    if f.endswith(".dll"):
                        shutil.copy2(c("ffmpeg", "bin", f), os.path.join(a.prefix, "bin", f))
                print("ffmpeg: staged", flush=True)
            else:
                print("ffmpeg: not fetched (its pin is not confirmed yet): the movies are skipped", flush=True)
    except (OSError, RuntimeError) as e:
        print(f"build_win_deps: {e}\n(the whole log: {log_path})", file=sys.stderr)
        return 1
    shutil.rmtree(tmp, ignore_errors=True)
    with open(stamp, "w", encoding="utf-8") as f:
        json.dump(want, f, indent=1)
    print(f"build_win_deps: {a.prefix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
