// affin: list the threads of a process with start address, user/kernel time and affinity, and optionally set the
// affinity of the threads that start at one module offset (and of all other threads).
// usage: affin.exe <exename> <outfile> [start_rva_hex mask_hex [others_mask_hex]]
//   e.g. affin.exe RelicCardinal.exe D:\aoe\affin.txt 3e1b04c 80 7f   -> loop thread on core 7, the rest on 0-6
// Wine applies a thread affinity change with sched_setaffinity on the thread's unix tid (wineserver side).
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>

typedef NTSTATUS (WINAPI *QIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef struct { NTSTATUS ExitStatus; PVOID TebBaseAddress; CLIENT_ID ClientId; ULONG_PTR AffinityMask; LONG Priority; LONG BasePriority; } TBI;

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    FILE *out = fopen(argv[2], "w");
    if (!out) return 1;
    ULONG64 rva = argc > 4 ? strtoull(argv[3], NULL, 16) : 0;
    ULONG_PTR mask = argc > 4 ? (ULONG_PTR)strtoull(argv[4], NULL, 16) : 0;
    ULONG_PTR others = argc > 5 ? (ULONG_PTR)strtoull(argv[5], NULL, 16) : 0;
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
    ULONG64 base = (ULONG64)exe;
    fprintf(out, "pid=%lu base=%llx\n", pid, base);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION, FALSE, te.th32ThreadID);
        if (!t) continue;
        PVOID start = NULL;
        TBI tbi = { 0 };
        qit(t, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), NULL);
        qit(t, 0 /* ThreadBasicInformation */, &tbi, sizeof(tbi), NULL);
        FILETIME c, e, k, u;
        ULONG64 ut = 0, kt = 0;
        if (GetThreadTimes(t, &c, &e, &k, &u)) {
            ut = ((ULONG64)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10000;
            kt = ((ULONG64)k.dwHighDateTime << 32 | k.dwLowDateTime) / 10000;
        }
        ULONG64 s = (ULONG64)start;
        int match = rva && s == base + rva;
        ULONG_PTR prev = 0;
        if (match && mask) prev = SetThreadAffinityMask(t, mask);
        else if (!match && others) prev = SetThreadAffinityMask(t, others);
        int inexe = s >= base && s < base + 0x8000000;
        fprintf(out, "tid %04lx start=%s%llx user=%llu kernel=%llu affinity=%llx%s set_prev=%llx\n", te.th32ThreadID,
                inexe ? "exe+" : "", inexe ? s - base : s, ut, kt, (ULONG64)tbi.AffinityMask, match ? " LOOP" : "",
                (ULONG64)prev);
        CloseHandle(t);
    }
    CloseHandle(snap);
    fprintf(out, "END\n");
    fclose(out);
    return 0;
}
