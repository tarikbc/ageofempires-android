// vmmap: summarize a process's address space and try to start one remote thread in it.
// usage: vmmap.exe <exename> <outfile>
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

static FILE *out;
#define LOG(...) do { fprintf(out, __VA_ARGS__); fflush(out); } while (0)

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    out = fopen(argv[2], "w");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) { pid = pe.th32ProcessID; LOG("pid=0x%lx threads=%lu\n", pid, pe.cntThreads); }
    CloseHandle(snap);
    HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    LOG("OpenProcess=%p err=%lu\n", p, p ? 0 : GetLastError());
    if (!p) return 1;

    const ULONG64 limits[] = { 0x80000000ull, 0x100000000ull, 0x8000000000ull };
    ULONG64 freeb[3] = {0}, largest[3] = {0}, commit = 0, reserve = 0, image = 0, mapped = 0;
    MEMORY_BASIC_INFORMATION mbi;
    ULONG64 a = 0x10000, toplow = 0;
    while (VirtualQueryEx(p, (void *)a, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        ULONG64 b = (ULONG64)mbi.BaseAddress, s = mbi.RegionSize;
        for (int i = 0; i < 3; i++) {
            if (mbi.State == MEM_FREE && b < limits[i]) {
                ULONG64 e = b + s > limits[i] ? limits[i] : b + s;
                freeb[i] += e - b;
                if (e - b > largest[i]) largest[i] = e - b;
            }
        }
        if (mbi.State == MEM_COMMIT) commit += s;
        if (mbi.State == MEM_RESERVE) reserve += s;
        if (mbi.State != MEM_FREE && mbi.Type == MEM_IMAGE) image += s;
        if (mbi.State != MEM_FREE && mbi.Type == MEM_MAPPED) mapped += s;
        if (mbi.State != MEM_FREE && b < 0x80000000ull && s >= 0x4000000)
            LOG("  low big region %llx size %lluMB state %lx type %lx\n", b, s >> 20, mbi.State, mbi.Type);
        if (mbi.State != MEM_FREE && b < 0x80000000ull) toplow = b + s;
        a = b + s;
        if (a >= 0x8000000000ull) break;
    }
    LOG("commit=%lluMB reserve=%lluMB image=%lluMB mapped=%lluMB\n", commit >> 20, reserve >> 20, image >> 20, mapped >> 20);
    for (int i = 0; i < 3; i++)
        LOG("below %llx: free=%lluMB largest=%lluMB\n", limits[i], freeb[i] >> 20, largest[i] >> 20);

    HANDLE t = CreateRemoteThread(p, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep"), (void *)1, 0, NULL);
    LOG("CreateRemoteThread default stack=%p err=%lu\n", t, t ? 0 : GetLastError());
    if (t) CloseHandle(t);
    t = CreateRemoteThread(p, NULL, 0x10000, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep"), (void *)1, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    LOG("CreateRemoteThread 64k stack=%p err=%lu\n", t, t ? 0 : GetLastError());
    if (t) CloseHandle(t);

    HANDLE self = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Sleep, (void *)1, 0, NULL);
    LOG("local CreateThread=%p err=%lu\n", self, self ? 0 : GetLastError());
    LOG("END\n");
    return 0;
}
