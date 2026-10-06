// hcount: sample a process's handle counts over time, to test for handle/socket exhaustion.
// usage: hcount.exe <exe-name> <outfile> <seconds> [interval_ms]
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD find_pid(const char *name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (!_stricmp(pe.szExeFile, name)) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: hcount.exe <exe> <out> <seconds> [interval_ms]\n"); return 1; }
    const char *name = argv[1];
    FILE *out = fopen(argv[2], "w");
    if (!out) return 1;
    int seconds = atoi(argv[3]);
    int interval = argc > 4 ? atoi(argv[4]) : 5000;

    fprintf(out, "sampling handles of %s for %ds every %dms\n", name, seconds, interval);
    fflush(out);

    DWORD t0 = GetTickCount();
    DWORD first = 0, last = 0;
    int samples = 0;
    while ((int)((GetTickCount() - t0) / 1000) < seconds) {
        DWORD pid = find_pid(name);
        if (!pid) {
            fprintf(out, "[%4lus] process not found (exited?)\n", (GetTickCount() - t0) / 1000);
            fflush(out);
            break;
        }
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!h) {
            fprintf(out, "[%4lus] OpenProcess(%lu) failed err=%lu\n", (GetTickCount() - t0) / 1000, pid, GetLastError());
            fflush(out);
        } else {
            DWORD handles = 0;
            GetProcessHandleCount(h, &handles);
            DWORD gdi = GetGuiResources(h, GR_GDIOBJECTS);
            DWORD usr = GetGuiResources(h, GR_USEROBJECTS);
            PROCESS_MEMORY_COUNTERS pmc = { sizeof(pmc) };
            GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
            if (!samples) first = handles;
            last = handles;
            samples++;
            fprintf(out, "[%4lus] pid=%lu handles=%lu gdi=%lu user=%lu rss=%luMB\n",
                    (GetTickCount() - t0) / 1000, pid, handles, gdi, usr,
                    (unsigned long)(pmc.WorkingSetSize / (1024 * 1024)));
            fflush(out);
            CloseHandle(h);
        }
        Sleep(interval);
    }
    if (samples >= 2)
        fprintf(out, "\nfirst=%lu last=%lu delta=%ld over %d samples\n",
                first, last, (long)last - (long)first, samples);
    fprintf(out, "END\n");
    fclose(out);
    return 0;
}
