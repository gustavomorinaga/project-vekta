#!/usr/bin/env python3
"""Build and stage the Linux build's libraries into one prefix (default work/linux-deps), from the pinned sources and
packages tools/release/fetch_toolchain.py --host native unpacked, with the pinned Zig (port/cmake/zig-linux.cmake):

    tools/release/build_linux_deps.py [--cache DIR] [--prefix DIR] [--jobs N]

  SDL2            built from source, shared, with no video or audio drivers: only to link against (its headers, its
                  CMake package and libSDL2-2.0.so). The game runs with the system's libSDL2-2.0.so.0, which carries
                  the system's X11, Wayland and audio drivers; SDL2 keeps one ABI across 2.x.
  zlib            built from source, static (lib/libz.a)
  glslang         built from source, static, HLSL front end on, no optimizer, no tools (as for Windows)
  Vulkan headers  include/vulkan, include/vk_video (the Vulkan loader is opened at run time)
  FFmpeg          include/lib* (the movie loader's headers) and ffmpeg/lib*.so.<major> (loaded at run time:
                  tools/kzbuild.py copies them to <build>/lib, which tools/play.sh puts on LD_LIBRARY_PATH)

The prefix's stamp.json records the pins it was made from; a run with the same pins does nothing. CMake and Ninja:
the pinned ones (cmake-linux, ninja-linux). Exit 0 = staged, 1 = a step failed. Standard library only.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import fetch_toolchain  # noqa: E402

TOOLCHAIN_FILE = os.path.join(ROOT, "port", "cmake", "zig-linux.cmake")
PARTS = ("zig", "cmake-linux", "ninja-linux", "sdl2-src", "zlib", "glslang", "vulkan-headers", "ffmpeg-linux")
FFMPEG_LIBS = ("libavcodec.", "libavformat.", "libavutil.", "libswscale.", "libswresample.")
SONAME_RX = re.compile(r"(lib\w+\.so\.\d+)\.\d+\.\d+")


def copytree(src, dst):
    for d, _, files in os.walk(src):
        rel = os.path.relpath(d, src)
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in files:
            s, t = os.path.join(d, f), os.path.join(dst, rel, f)
            if os.path.islink(s):
                if os.path.lexists(t):
                    os.remove(t)
                os.symlink(os.readlink(s), t)
            else:
                shutil.copy2(s, t)


def tool_env(cache):
    """PATH with the pinned CMake and Ninja first; the Zig wrappers find Zig in the cache."""
    e = dict(os.environ)
    e["PATH"] = os.pathsep.join([os.path.join(cache, "cmake-linux", "bin"), os.path.join(cache, "ninja-linux"),
                                 e.get("PATH", "")])
    e["KZ_ZIG"] = os.path.join(cache, "zig", "zig")
    return e


def run(argv, log, env):
    print("$ " + " ".join(argv), file=log, flush=True)
    r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    log.write(r.stdout)
    if r.returncode:
        tail = "\n".join(r.stdout.strip().splitlines()[-15:])
        raise RuntimeError(f"{argv[0]} failed (exit {r.returncode}):\n{tail}")


def cmake_build(name, src, build, prefix, jobs, options, log, env):
    if os.path.isdir(build):
        shutil.rmtree(build)
    run(["cmake", "-G", "Ninja", "-S", src, "-B", build, f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN_FILE}",
         f"-DKZ_LINUX_DEPS={prefix}", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix}", *options],
        log, env)
    run(["cmake", "--build", build, "-j", str(jobs)], log, env)
    run(["cmake", "--install", build], log, env)
    print(f"{name}: built", flush=True)


def make_executable(cache):
    """A wheel (Zig's) and a zip (Ninja's) may carry no file modes: the programs must run."""
    for p in (os.path.join(cache, "zig", "zig"), os.path.join(cache, "ninja-linux", "ninja")):
        if os.path.isfile(p):
            os.chmod(p, os.stat(p).st_mode | 0o755)


def stamp_of(m):
    pins = {e["name"]: e["sha256"] for e in m["components"] if e["name"] in PARTS}
    return {"pins": pins, "toolchain_file": fetch_toolchain.sha256_file(TOOLCHAIN_FILE),
            "wrapper": fetch_toolchain.sha256_file(os.path.join(HERE, "zig", "cc"))}


# SDL2 for linking only: every driver that would need the system's headers is off.
SDL2_OPTIONS = ["-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST=OFF", "-DSDL2_DISABLE_SDL2MAIN=ON",
                "-DSDL_X11=OFF", "-DSDL_WAYLAND=OFF", "-DSDL_KMSDRM=OFF", "-DSDL_OPENGL=OFF", "-DSDL_OPENGLES=OFF",
                "-DSDL_VULKAN=ON", "-DSDL_ALSA=OFF", "-DSDL_PULSEAUDIO=OFF", "-DSDL_PIPEWIRE=OFF", "-DSDL_JACK=OFF",
                "-DSDL_SNDIO=OFF", "-DSDL_OSS=OFF", "-DSDL_LIBUDEV=OFF", "-DSDL_DBUS=OFF", "-DSDL_IBUS=OFF",
                "-DSDL_HIDAPI_LIBUSB=OFF", "-DSDL_RPATH=OFF", "-DSDL_LIBSAMPLERATE=OFF", "-DSDL_DIRECTFB=OFF",
                "-DSDL_RPI=OFF", "-DSDL_VIVANTE=OFF", "-DSDL_OFFSCREEN=ON"]


def main(argv=None):
    work = os.environ.get("KZ_WORK") or os.path.join(ROOT, "work")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cache", default=os.path.join(work, "toolchain-cache"))
    ap.add_argument("--prefix", default=os.path.join(work, "linux-deps"))
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("KZ_JOBS") or os.cpu_count() or 4))
    a = ap.parse_args(argv)
    c = lambda *p: os.path.join(a.cache, *p)  # noqa: E731
    need = [n for n in PARTS if not os.path.isdir(c(n))]
    if need:
        print(f"build_linux_deps: not fetched yet: {', '.join(need)} (tools/release/fetch_toolchain.py --host native"
              " and --only zlib,glslang,vulkan-headers)", file=sys.stderr)
        return 1
    make_executable(a.cache)
    m = fetch_toolchain.load()
    want = stamp_of(m)
    stamp = os.path.join(a.prefix, "stamp.json")
    try:
        with open(stamp, encoding="utf-8") as f:
            if json.load(f) == want:
                print(f"build_linux_deps: {a.prefix} is current")
                return 0
    except (OSError, ValueError):
        pass
    if os.path.isdir(a.prefix):
        shutil.rmtree(a.prefix)
    os.makedirs(a.prefix)
    tmp = os.path.join(os.path.dirname(a.prefix), "linux-deps-build")
    log_path = os.path.join(os.path.dirname(a.prefix), "linux-deps.log")
    env = tool_env(a.cache)
    env["KZ_WORK"] = work
    try:
        with open(log_path, "w", encoding="utf-8") as log:
            for name in ("vk_video", "vulkan"):
                copytree(c("vulkan-headers", "include", name), os.path.join(a.prefix, "include", name))
            print("vulkan-headers: staged", flush=True)
            cmake_build("sdl2", c("sdl2-src"), os.path.join(tmp, "sdl2"), a.prefix, a.jobs, SDL2_OPTIONS, log, env)
            cmake_build("zlib", c("zlib"), os.path.join(tmp, "zlib"), a.prefix, a.jobs,
                        ["-DZLIB_BUILD_EXAMPLES=OFF"], log, env)
            # zlib linked statically: no shared library for find_package(ZLIB) to prefer
            for f in os.listdir(os.path.join(a.prefix, "lib")):
                if f.startswith("libz.so"):
                    os.remove(os.path.join(a.prefix, "lib", f))
            cmake_build("glslang", c("glslang"), os.path.join(tmp, "glslang"), a.prefix, a.jobs,
                        ["-DBUILD_SHARED_LIBS=OFF", "-DENABLE_OPT=OFF", "-DENABLE_HLSL=ON", "-DGLSLANG_TESTS=OFF",
                         "-DENABLE_GLSLANG_BINARIES=OFF", "-DBUILD_EXTERNAL=OFF", "-DENABLE_SPVREMAPPER=OFF"],
                        log, env)
            for d in os.listdir(c("ffmpeg-linux", "include")):
                copytree(c("ffmpeg-linux", "include", d), os.path.join(a.prefix, "include", d))
            ff = os.path.join(a.prefix, "ffmpeg")
            os.makedirs(ff)
            # each library under the name the loader asks for (libavcodec.so.63): the real file, whether the tree
            # still has the package's symlinks or (a release folder) only libavcodec.so.63.1.102
            for f in sorted(os.listdir(c("ffmpeg-linux", "lib"))):
                m = SONAME_RX.fullmatch(f)
                if m and f.startswith(FFMPEG_LIBS) and not os.path.islink(c("ffmpeg-linux", "lib", f)):
                    shutil.copy2(c("ffmpeg-linux", "lib", f), os.path.join(ff, m.group(1)))
            print("ffmpeg: staged", flush=True)
    except (OSError, RuntimeError) as e:
        print(f"build_linux_deps: {e}\n(the whole log: {log_path})", file=sys.stderr)
        return 1
    shutil.rmtree(tmp, ignore_errors=True)
    with open(stamp, "w", encoding="utf-8") as f:
        json.dump(want, f, indent=1)
    print(f"build_linux_deps: {a.prefix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
