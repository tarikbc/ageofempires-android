// tduty: duty-cycle the threads of a process that start at one module offset: repeatedly suspend them for OFF ms and
// let them run for ON ms, for a total time. Used to measure how throttling one thread (the protection's loop) changes
// the others' speed.
// usage: tduty.exe <exename> <start_rva_hex> <off_ms> <on_ms> <total_ms> <outfile>
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>

typedef NTSTATUS (WINAPI *QIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);

int main(int argc, char **argv)
{
    if (argc < 7) return 1;
    FILE *out = fopen(argv[6], "w");
    if (!out) return 1;
    ULONG64 rva = strtoull(argv[2], NULL, 16);
    DWORD off = (DWORD)atoi(argv[3]), on = (DWORD)atoi(argv[4]), total = (DWORD)atoi(argv[5]);
    QIT qit = (QIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { fprintf(out, "pid=%lu open failed\nEND\n", pid); fclose(out); return 1; }
    HMODULE exe = NULL;
    DWORD need = 0;
    EnumProcessModules(proc, &exe, sizeof(exe), &need);
    ULONG64 target = (ULONG64)exe + rva;
    HANDLE ths[16];
    int n = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok && n < 16; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        PVOID start = NULL;
        qit(t, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), NULL);
        if ((ULONG64)start == target) {
            fprintf(out, "thread %04lx matches\n", te.th32ThreadID);
            ths[n++] = t;
        } else {
            CloseHandle(t);
        }
    }
    CloseHandle(snap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(out, "%02d:%02d:%02d.%03d start: %d thread(s), off %lu ms, on %lu ms, total %lu ms\n", st.wHour, st.wMinute,
            st.wSecond, st.wMilliseconds, n, off, on, total);
    fflush(out);
    for (DWORD elapsed = 0; elapsed < total; elapsed += off + on) {
        for (int i = 0; i < n; i++) SuspendThread(ths[i]);
        Sleep(off);
        for (int i = 0; i < n; i++) ResumeThread(ths[i]);
        Sleep(on);
    }
    GetLocalTime(&st);
    fprintf(out, "%02d:%02d:%02d.%03d done\nEND\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    fclose(out);
    return 0;
}
