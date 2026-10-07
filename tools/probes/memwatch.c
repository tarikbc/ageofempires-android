// memwatch: poll a memory range of a running process and log every 8-byte value that changes, with a time stamp.
//
// usage: memwatch.exe <exename> <base_hex> <size_hex> <interval_ms> <outfile>
// Waits up to 10 minutes for the process, then reads the range every interval until the process exits or the
// range cannot be read. The first read is logged in full (non-zero qwords only); later lines are changes:
//   hh:mm:ss.mmm +<offset> <old> -> <new>
// Build: x86_64-w64-mingw32-gcc -O1 -static -o memwatch.exe memwatch.c
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD find_pid(const char *exe)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {.dwSize = sizeof(pe)};
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!lstrcmpiA(pe.szExeFile, exe)) { pid = pe.th32ProcessID; break; }
    CloseHandle(snap);
    return pid;
}

static void stamp(char *buf, size_t n)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(buf, n, "%02d:%02d:%02d.%03d", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: memwatch.exe <exename> <base_hex> <size_hex> <interval_ms> <outfile>\n");
        return 1;
    }
    ULONG64 base = _strtoui64(argv[2], NULL, 16);
    size_t size = (size_t)_strtoui64(argv[3], NULL, 16) & ~(size_t)7;
    DWORD interval = (DWORD)atoi(argv[4]);
    FILE *f = fopen(argv[5], "w");
    if (!f) return 1;
    char ts[32];
    DWORD pid = 0;
    for (int i = 0; i < 6000 && !(pid = find_pid(argv[1])); i++) Sleep(100);
    stamp(ts, sizeof(ts));
    if (!pid) { fprintf(f, "%s %s not found\n", ts, argv[1]); fclose(f); return 2; }
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) { fprintf(f, "%s OpenProcess failed %lu\n", ts, GetLastError()); fclose(f); return 3; }
    fprintf(f, "%s watching pid %lu range %llx+%zx every %lu ms\n", ts, pid, base, size, interval);
    fflush(f);
    unsigned long long *prev = calloc(size / 8, 8), *cur = calloc(size / 8, 8);
    int first = 1;
    unsigned long reads = 0;
    for (;;) {
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, (LPCVOID)base, cur, size, &got) || got != size) {
            stamp(ts, sizeof(ts));
            fprintf(f, "%s read failed (%lu), got %zu, after %lu reads\n", ts, GetLastError(), (size_t)got, reads);
            break;
        }
        reads++;
        stamp(ts, sizeof(ts));
        for (size_t q = 0; q < size / 8; q++) {
            if (first ? cur[q] != 0 : cur[q] != prev[q])
                fprintf(f, "%s +%zx %016llx -> %016llx\n", ts, q * 8, prev[q], cur[q]);
        }
        fflush(f);
        memcpy(prev, cur, size);
        first = 0;
        if (WaitForSingleObject(h, interval) == WAIT_OBJECT_0) {
            stamp(ts, sizeof(ts));
            fprintf(f, "%s process exited after %lu reads\n", ts, reads);
            break;
        }
    }
    fclose(f);
    return 0;
}
