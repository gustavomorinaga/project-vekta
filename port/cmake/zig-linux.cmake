# CMake toolchain file: the Linux build with the pinned Zig (tools/release/toolchain.json, entry zig), through the
# wrappers in tools/release/zig/ (target x86_64-linux-gnu.2.31: glibc's headers and libc++ come with Zig, nothing from
# the system). KZ_LINUX_DEPS: the prefix tools/release/build_linux_deps.py staged (SDL2 to link against, zlib,
# glslang, the Vulkan and FFmpeg headers); find_* looks only there for libraries, headers and packages.
# Used by tools/kzbuild.py build when the pinned Linux toolchain is on (the Linux release folder, or
# KZ_LINUX_TOOLCHAIN=pinned in a clone).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT KZ_LINUX_DEPS AND DEFINED ENV{KZ_LINUX_DEPS})
    set(KZ_LINUX_DEPS "$ENV{KZ_LINUX_DEPS}")
endif()
set(KZ_LINUX_DEPS "${KZ_LINUX_DEPS}" CACHE PATH "the Linux libraries' prefix (tools/release/build_linux_deps.py)")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES KZ_LINUX_DEPS)

get_filename_component(_kz_zig "${CMAKE_CURRENT_LIST_DIR}/../../tools/release/zig" ABSOLUTE)
set(CMAKE_C_COMPILER   "${_kz_zig}/cc")
set(CMAKE_CXX_COMPILER "${_kz_zig}/c++")
set(CMAKE_AR           "${_kz_zig}/ar" CACHE FILEPATH "archiver")
set(CMAKE_RANLIB       "${_kz_zig}/ranlib" CACHE FILEPATH "ranlib")

if(KZ_LINUX_DEPS)
    set(CMAKE_FIND_ROOT_PATH "${KZ_LINUX_DEPS}")
    list(APPEND CMAKE_PREFIX_PATH "${KZ_LINUX_DEPS}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
