/* Wine host test for KZ1HD.exe's resources (port/kz1hd/app.rc): tools/wine-host-tests.sh compiles app.rc, with the
 * kz_version.h version.cmake writes, into this exe in a folder whose path has a space, and runs it under Wine. It
 * reads its own version block back through GetFileVersionInfoW (what Explorer's Details tab shows) and its manifest
 * through FindResource. No game data. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "kz_version.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { fails++; printf("FAIL %s\n", what); } else printf("ok   %s\n", what); } while (0)

static const wchar_t* str(void* blk, const wchar_t* name)
{
    wchar_t key[96];
    swprintf(key, 96, L"\\StringFileInfo\\040904B0\\%ls", name);
    wchar_t* v = NULL;
    UINT n = 0;
    return VerQueryValueW(blk, key, (void**)&v, &n) && n ? v : L"";
}

int main(void)
{
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    CHECK(len && wcschr(path, L' '), "the exe sits in a folder with a space in its path");
    DWORD h = 0, size = GetFileVersionInfoSizeW(path, &h);
    static char blk[16384];
    CHECK(size && size <= sizeof blk && GetFileVersionInfoW(path, 0, sizeof blk, blk), "GetFileVersionInfoW reads it");
    VS_FIXEDFILEINFO* ffi = NULL;
    UINT n = 0;
    unsigned want[4] = {0};
    sscanf(VEKTA_VER_STR, "%u.%u.%u", &want[0], &want[1], &want[2]);
    CHECK(VerQueryValueW(blk, L"\\", (void**)&ffi, &n) && ffi && HIWORD(ffi->dwFileVersionMS) == want[0] &&
          LOWORD(ffi->dwFileVersionMS) == want[1] && HIWORD(ffi->dwFileVersionLS) == want[2] &&
          LOWORD(ffi->dwFileVersionLS) == 0, "the file version is VERSION with a fourth 0");
    wchar_t ver[32];
    swprintf(ver, 32, L"%hs", VEKTA_VER_STR);
    CHECK(!wcscmp(str(blk, L"FileVersion"), ver) && !wcscmp(str(blk, L"ProductVersion"), ver),
          "FileVersion and ProductVersion say VERSION");
    CHECK(!wcscmp(str(blk, L"ProductName"), L"Project Vekta") && !wcscmp(str(blk, L"FileDescription"), L"Project Vekta"),
          "product and description: Project Vekta");
    CHECK(!wcscmp(str(blk, L"CompanyName"), L"Project Vekta contributors"), "company: Project Vekta contributors");
    CHECK(!wcscmp(str(blk, L"OriginalFilename"), L"KZ1HD.exe"), "original file name: KZ1HD.exe");
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));   /* RT_MANIFEST */
    const char* m = r ? (const char*)LockResource(LoadResource(NULL, r)) : NULL;
    DWORD ms = r ? SizeofResource(NULL, r) : 0;
    char text[4096] = {0};
    if (m && ms < sizeof text) memcpy(text, m, ms);
    CHECK(strstr(text, "{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}") && strstr(text, "PerMonitorV2"),
          "the manifest (resource 1, RT_MANIFEST) names Windows 10/11 and per-monitor v2");
    CHECK(FindResourceW(NULL, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14)) != NULL, "the icon group is there");  /* RT_GROUP_ICON */
    printf("test_exe_resources: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
