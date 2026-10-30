/*
 * POSIX names the port's host code uses, on Windows (MinGW). Force-included
 * into the port's own sources by CMakeLists.txt on WIN32 only.
 */
#ifndef KZ_WIN_COMPAT_H
#define KZ_WIN_COMPAT_H
#ifdef _WIN32
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
/* setenv(name, value, overwrite): the CRT's _putenv_s, which getenv sees */
static inline int setenv(const char* name, const char* value, int overwrite)
{
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value) ? -1 : 0;
}
static inline int unsetenv(const char* name) { return _putenv_s(name, "") ? -1 : 0; }
#include <string.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif
#ifdef __cplusplus
}
#endif
#endif
#endif
