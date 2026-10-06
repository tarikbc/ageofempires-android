// filescan: recursively scan files under a directory for a byte pattern.
// usage: filescan.exe <root> <pattern> <outfile> [maxdepth]
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static FILE *out;
static const char *pat;
static size_t patlen;
static int maxdepth = 6;
static int scanned;

static void scan_file(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (LONGLONG)400 * 1024 * 1024) { CloseHandle(h); return; }
    DWORD n = (DWORD)sz.QuadPart;
    char *buf = malloc(n + 1);
    if (!buf) { CloseHandle(h); return; }
    DWORD got = 0;
    if (ReadFile(h, buf, n, &got, NULL) && got) {
        scanned++;
        for (DWORD i = 0; i + patlen <= got; i++) {
            if (!memcmp(buf + i, pat, patlen)) {
                fprintf(out, "MATCH %s at offset 0x%lx\n", path, (unsigned long)i);
                fflush(out);
                break;
            }
        }
        // also try a second, alternated search: pattern with each byte as-is is enough
    }
    free(buf);
    CloseHandle(h);
}

static void walk(const char *dir, int depth)
{
    char spec[MAX_PATH];
    snprintf(spec, sizeof(spec), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(spec, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < maxdepth) walk(path, depth + 1);
        } else {
            scan_file(path);
        }
    } while (FindNextFileA(f, &fd));
    FindClose(f);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: filescan.exe <root> <pattern> <outfile> [maxdepth]\n"); return 1; }
    out = fopen(argv[3], "w");
    if (!out) return 1;
    pat = argv[2];
    patlen = strlen(pat);
    if (argc > 4) maxdepth = atoi(argv[4]);
    fprintf(out, "scan root=%s pattern=%s depth=%d\n", argv[1], pat, maxdepth);
    walk(argv[1], 0);
    fprintf(out, "scanned %d files\nEND\n", scanned);
    fclose(out);
    return 0;
}
