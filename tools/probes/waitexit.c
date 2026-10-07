// waitexit: wait for a process to end, then record its exit code and copy the game log at once.
//
// When AoE IV exits (instead of freezing), GameNative shuts the whole container down at once, so the
// usual 10 s log copies miss the end and nothing records why the process ended. This probe holds only a
// SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION handle, so it does not touch the game while it runs.
// usage: waitexit.exe [exename] [outfile]   defaults: RelicCardinal.exe D:\aoe\exitcode.txt
// Also copies %USERPROFILE%\Documents\My Games\Age of Empires IV\warnings.log to D:\aoe\final_log.txt.
// Build: x86_64-w64-mingw32-gcc -O1 -static -o waitexit.exe waitexit.c
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

static DWORD find_pid(const char *exe)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {.dwSize = sizeof(pe)};
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe)) {
        if (!lstrcmpiA(pe.szExeFile, exe)) { pid = pe.th32ProcessID; break; }
    }
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
    const char *exe = argc > 1 ? argv[1] : "RelicCardinal.exe";
    const char *outp = argc > 2 ? argv[2] : "D:\\aoe\\exitcode.txt";
    char ts[32], log[MAX_PATH];
    DWORD pid = 0;
    for (int i = 0; i < 600 && !(pid = find_pid(exe)); i++) Sleep(1000);
    FILE *f = fopen(outp, "w");
    if (!f) return 1;
    stamp(ts, sizeof(ts));
    if (!pid) { fprintf(f, "%s %s not found\n", ts, exe); fclose(f); return 2; }
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) { fprintf(f, "%s OpenProcess(%lu) failed: %lu\n", ts, pid, GetLastError()); fclose(f); return 3; }
    fprintf(f, "%s waiting for %s pid %lu\n", ts, exe, pid);
    fflush(f);
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    BOOL got = GetExitCodeProcess(h, &code);
    stamp(ts, sizeof(ts));
    fprintf(f, "%s exited, exit code %s0x%08lx (%ld)\n", ts, got ? "" : "unknown ", code, (long)code);
    fflush(f);
    snprintf(log, sizeof(log), "%s\\Documents\\My Games\\Age of Empires IV\\warnings.log", getenv("USERPROFILE"));
    BOOL copied = CopyFileA(log, "D:\\aoe\\final_log.txt", FALSE);
    stamp(ts, sizeof(ts));
    fprintf(f, "%s log copy %s\n", ts, copied ? "done" : "failed");
    fclose(f);
    return 0;
}
