// gcopy: copy files inside the Wine session without opening a console window. Paths may use %VARIABLES%, for example
// "%USERPROFILE%\Documents\My Games\Age of Empires IV\warnings.log".
// usage: gcopy.exe <src> <dst>, or gcopy.exe alone: reads pairs of lines (src, dst) from D:\aoe\gcopy.txt
// (a winhandler launch request holds only 51 bytes of program and arguments, too few for most paths).
// Build: x86_64-w64-mingw32-gcc -O1 -static -mwindows -o gcopy.exe gcopy.c -lshell32
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

static BOOL copy(const WCHAR *src, const WCHAR *dst)
{
    WCHAR s[MAX_PATH], d[MAX_PATH];
    if (!ExpandEnvironmentStringsW(src, s, MAX_PATH) || !ExpandEnvironmentStringsW(dst, d, MAX_PATH)) return FALSE;
    return CopyFileW(s, d, FALSE);
}

static void chomp(WCHAR *s)
{
    size_t n = wcslen(s);
    while (n && (s[n - 1] == L'\n' || s[n - 1] == L'\r')) s[--n] = 0;
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3) return copy(argv[1], argv[2]) ? 0 : 3;
    FILE *f = _wfopen(L"D:\\aoe\\gcopy.txt", L"r");
    if (!f) return 1;
    WCHAR src[MAX_PATH], dst[MAX_PATH];
    int failed = 0;
    while (fgetws(src, MAX_PATH, f) && fgetws(dst, MAX_PATH, f)) {
        chomp(src);
        chomp(dst);
        if (!copy(src, dst)) failed++;
    }
    fclose(f);
    return failed ? 3 : 0;
}
