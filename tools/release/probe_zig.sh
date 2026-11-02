#!/usr/bin/env bash
# TEMPORARY probe (see .github/workflows/linux-probe.yml).
set -euxo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$RUNNER_TEMP/zt"; mkdir -p "$T/dl" "$T/src" "$T/prefix" "$T/bin"
cd "$T/dl"
dl() { curl -sSfL -o "$2" "$1"; echo "$3  $2" | sha256sum -c -; }
dl https://files.pythonhosted.org/packages/py3/z/ziglang/ziglang-0.17.0-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.musllinux_1_1_x86_64.whl zig.whl 97079827064a7492cce5541ceb438ae8003ce1e54e399676da2d62fb7a5dc08b || {
  python3 -m pip download --no-deps --only-binary=:all: --platform manylinux_2_12_x86_64 ziglang==0.17.0 -d .
  mv ziglang-*.whl zig.whl; sha256sum zig.whl; }
python3 -m zipfile -e zig.whl "$T/zigwhl"
Z="$T/zigwhl/ziglang/zig"; chmod +x "$Z"; "$Z" version
python3 -m pip install --quiet cmake==4.1.2 ninja==1.13.0 || python3 -m pip install --quiet --break-system-packages cmake ninja
cmake --version | head -1
TGT=x86_64-linux-gnu.2.31
cat > "$T/bin/zcc" <<EOS
#!/bin/sh
exec "$Z" cc -target $TGT "\$@"
EOS
cat > "$T/bin/zc++" <<EOS
#!/bin/sh
exec "$Z" c++ -target $TGT "\$@"
EOS
cat > "$T/bin/zar" <<EOS
#!/bin/sh
exec "$Z" ar "\$@"
EOS
cat > "$T/bin/zranlib" <<EOS
#!/bin/sh
exec "$Z" ranlib "\$@"
EOS
chmod +x "$T/bin/"*
cat > "$T/zig.cmake" <<EOS
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "$T/bin/zcc")
set(CMAKE_CXX_COMPILER "$T/bin/zc++")
set(CMAKE_AR "$T/bin/zar" CACHE FILEPATH "")
set(CMAKE_RANLIB "$T/bin/zranlib" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH "$T/prefix")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOS
# sources
dl https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz zlib.tgz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
dl https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz sdl.tgz - || curl -sSfL -o sdl.tgz https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
sha256sum sdl.tgz
curl -sSfL -o glslang.tgz https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz
curl -sSfL -o vkh.tgz https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.363.tar.gz
for f in zlib sdl glslang vkh; do mkdir -p "$T/src/$f"; tar -xzf "$f.tgz" -C "$T/src/$f" --strip-components=1; done
TC="-DCMAKE_TOOLCHAIN_FILE=$T/zig.cmake"
cmake -G Ninja -S "$T/src/zlib" -B "$T/b/zlib" $TC -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$T/prefix" -DZLIB_BUILD_EXAMPLES=OFF
cmake --build "$T/b/zlib" && cmake --install "$T/b/zlib"
# SDL2: link-time library only (the system's libSDL2-2.0.so.0 at run time); no X11/Wayland headers here
cmake -G Ninja -S "$T/src/sdl" -B "$T/b/sdl" $TC -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$T/prefix" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF \
  -DSDL_OPENGLES=OFF -DSDL_VULKAN=ON -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF \
  -DSDL_SNDIO=OFF -DSDL_OSS=OFF -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_HIDAPI_LIBUSB=OFF \
  -DSDL_RPATH=OFF -DSDL_LIBSAMPLERATE=OFF -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_OFFSCREEN=ON
cmake --build "$T/b/sdl" && cmake --install "$T/b/sdl"
cmake -G Ninja -S "$T/src/glslang" -B "$T/b/glslang" $TC -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$T/prefix" \
  -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF -DENABLE_HLSL=ON -DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=OFF \
  -DBUILD_EXTERNAL=OFF -DENABLE_SPVREMAPPER=OFF
cmake --build "$T/b/glslang" && cmake --install "$T/b/glslang"
mkdir -p "$T/prefix/include"; cp -r "$T/src/vkh/include/vulkan" "$T/src/vkh/include/vk_video" "$T/prefix/include/"
P="$ROOT/work/ps3recomp-patched"
cmake -G Ninja -S "$P" -B "$T/b/ps3" $TC -DCMAKE_BUILD_TYPE=Release -DPS3RECOMP_RSX_VULKAN=ON \
  -DPS3RECOMP_VULKAN_INCLUDE_DIR="$T/prefix/include" -DCMAKE_PREFIX_PATH="$T/prefix" -DCMAKE_C_FLAGS=-O1 -DCMAKE_CXX_FLAGS=-O1
grep -E "Vulkan|SDL2|glslang" "$T/b/ps3/CMakeCache.txt" | head -20 || true
cmake --build "$T/b/ps3" -j "$(nproc)" -- -k 0 2>&1 | tail -60
ls -la "$T/b/ps3" | head -30
for f in "$T/b/ps3"/ps3recomp_host "$T/b/ps3"/*.so*; do [ -f "$f" ] && { file "$f"; readelf -d "$f" | grep NEEDED; objdump -T "$f" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1; }; done
