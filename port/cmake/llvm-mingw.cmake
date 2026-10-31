# Windows x64 with llvm-mingw (clang, lld, libc++ and libunwind, MinGW-w64 UCRT), the pinned compiler of
# tools/release/toolchain.json. Works with the Linux-hosted llvm-mingw (the cross build, tools/build-win.sh) and with
# the Windows-hosted one the installer will bundle. After Project Helghast's and Project Pyrrhus's
# llvm-mingw.cmake (undecuple, MIT).
#
#   cmake -G Ninja -S port/kz1hd -B work/build-win -DCMAKE_TOOLCHAIN_FILE=port/cmake/llvm-mingw.cmake \
#         -DLLVM_MINGW_ROOT=DIR -DKZ_WIN_DEPS=DIR
#
# LLVM_MINGW_ROOT holds bin/x86_64-w64-mingw32-clang; KZ_WIN_DEPS is the prefix tools/release/build_win_deps.py fills
# (SDL2, zlib, glslang, the Vulkan and FFmpeg headers), searched before the compiler's own sysroot. Both may come from
# the environment instead. -static links libc++, libunwind, compiler-rt and winpthreads into the exe (SDL2 stays a
# DLL: its CMake package names the import library). clang on MinGW uses native TLS, unlike GCC's emulated TLS.

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_SYSTEM_NAME Windows)
endif()
set(CMAKE_SYSTEM_PROCESSOR x86_64)

foreach(_v LLVM_MINGW_ROOT KZ_WIN_DEPS)
    if(NOT ${_v} AND DEFINED ENV{${_v}})
        set(${_v} "$ENV{${_v}}")
    endif()
endforeach()
set(LLVM_MINGW_ROOT "${LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw folder (holds bin/); empty = the compilers on PATH")
set(KZ_WIN_DEPS "${KZ_WIN_DEPS}" CACHE PATH "the Windows libraries' prefix (tools/release/build_win_deps.py)")
# try_compile projects read this file again: pass both on.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT KZ_WIN_DEPS)

set(_kz_pre "x86_64-w64-mingw32-")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_kz_exe ".exe")
else()
    set(_kz_exe "")
endif()
if(LLVM_MINGW_ROOT)
    set(_kz_bin "${LLVM_MINGW_ROOT}/bin/")
else()
    set(_kz_bin "")
endif()
set(CMAKE_C_COMPILER   "${_kz_bin}${_kz_pre}clang${_kz_exe}")
set(CMAKE_CXX_COMPILER "${_kz_bin}${_kz_pre}clang++${_kz_exe}")
set(CMAKE_RC_COMPILER  "${_kz_bin}${_kz_pre}windres${_kz_exe}")
set(CMAKE_AR           "${_kz_bin}llvm-ar${_kz_exe}" CACHE FILEPATH "archiver")
set(CMAKE_RANLIB       "${_kz_bin}llvm-ranlib${_kz_exe}" CACHE FILEPATH "ranlib")

set(CMAKE_FIND_ROOT_PATH "")
if(KZ_WIN_DEPS)
    list(APPEND CMAKE_FIND_ROOT_PATH "${KZ_WIN_DEPS}")
endif()
if(LLVM_MINGW_ROOT)
    list(APPEND CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Windows 10 API level (llvm-mingw's default is Windows 7, which hides GetCurrentThreadStackLimits).
set(CMAKE_C_FLAGS_INIT   "-D_WIN32_WINNT=0x0A00")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
