/* Building the FidelityFX SDK's VK backend and FSR3 upscaler for the Windows exe with llvm-mingw (libc++,
 * port/kz1hd/CMakeLists.txt): MinGW has the MSVC calls the SDK uses; what is missing are the headers MSVC's own
 * headers bring in by themselves, and _countof. */
#pragma once
#include <stdlib.h>
#include <wchar.h>
#ifdef __cplusplus
#include <cmath>
#include <cwchar>
#include <mutex>
#include <new>
#ifndef _countof
template <typename T, size_t N> constexpr size_t ffx_countof(T (&)[N]) { return N; }
#define _countof(a) ffx_countof(a)
#endif
#endif
