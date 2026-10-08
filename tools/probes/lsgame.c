// lsgame: list the files under the game's "My Games\Age of Empires IV" folder (two levels) into D:\aoe\ls.txt,
// with size and last-write time, without opening a console window.
#include <windows.h>
#include <stdio.h>

static FILE *out;

static void list(const WCHAR *dir, int depth)
{
    WCHAR pattern[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAW fd;
    swprintf(pattern, MAX_PATH, L"%ls\\*", dir);
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        SYSTEMTIME st;
        FileTimeToSystemTime(&fd.ftLastWriteTime, &st);
        ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        fwprintf(out, L"%*ls%ls%ls  %llu  %04d-%02d-%02d %02d:%02d\n", depth * 2, L"", fd.cFileName,
                 (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L"\\" : L"", size,
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && depth < 1) {
            swprintf(sub, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            list(sub, depth + 1);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d)
{
    WCHAR root[MAX_PATH];
    if (!GetEnvironmentVariableW(L"USERPROFILE", root, MAX_PATH)) return 1;
    lstrcatW(root, L"\\Documents\\My Games\\Age of Empires IV");
    out = _wfopen(L"D:\\aoe\\ls.txt", L"w");
    if (!out) return 2;
    list(root, 0);
    fclose(out);
    return 0;
}
