/* Building the FidelityFX SDK's VK backend and FSR3 upscaler on Linux (tools/ffx/build_fsr3.sh). */
#pragma once
#include <string.h>
#include <wchar.h>
#ifdef __cplusplus
#include <cstring>
#include <cwchar>
#include <codecvt>
#include <locale>
#include <string>
template <typename T, size_t N> constexpr size_t ffx_countof(T (&)[N]) { return N; }
#define _countof(a) ffx_countof(a)
#endif
#ifndef FFX_UNUSED
#define FFX_UNUSED(x) ((void)(x))
#endif
#include <stdio.h>
#include <math.h>
#ifdef __cplusplus
#include <cmath>
template <size_t N> inline int wcscpy_s(wchar_t (&d)[N], const wchar_t* s) { wcsncpy(d, s, N - 1); d[N - 1] = 0; return 0; }
inline int wcscpy_s(wchar_t* d, size_t n, const wchar_t* s) { wcsncpy(d, s, n - 1); d[n - 1] = 0; return 0; }
template <size_t N> inline int strcpy_s(char (&d)[N], const char* s) { strncpy(d, s, N - 1); d[N - 1] = 0; return 0; }
inline int strcpy_s(char* d, size_t n, const char* s) { strncpy(d, s, n - 1); d[n - 1] = 0; return 0; }
#define sprintf_s(buf, n, ...) snprintf((buf), (n), __VA_ARGS__)
#endif
