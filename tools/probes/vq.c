// vq: show the memory state of the pages around one address, and which thread owns the stack it is on.
// usage: vq.exe <hexaddr>   (target RelicCardinal.exe, output D:\vq.txt)
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { LONG ExitStatus; PVOID TebBaseAddress; ULONG_PTR cid[2]; ULONG_PTR Aff; LONG Pri, BasePri; } TBI;
int main(int argc, char **argv)
{
    FILE *f = fopen("D:\\vq.txt", "w");
    ULONG64 addr = _strtoui64(argv[1], NULL, 16);
    NTSTATUS (WINAPI *pNtQIT)(HANDLE, int, PVOID, ULONG, PULONG) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, "RelicCardinal.exe")) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    for (int k = -2; k <= 2; k++) {
        MEMORY_BASIC_INFORMATION m;
        ULONG64 a = (addr & ~0xfffull) + k * 0x1000;
        if (VirtualQueryEx(proc, (void *)a, &m, sizeof(m)))
            fprintf(f, "%llx: base=%p alloc=%p allocprot=%lx size=%llx state=%lx prot=%lx type=%lx\n", a, m.BaseAddress, m.AllocationBase, m.AllocationProtect, (ULONG64)m.RegionSize, m.State, m.Protect, m.Type);
    }
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        TBI tbi = { 0 };
        pNtQIT(t, 0, &tbi, sizeof(tbi), NULL);
        NT_TIB tib; SIZE_T got;
        if (ReadProcessMemory(proc, tbi.TebBaseAddress, &tib, sizeof(tib), &got) && addr >= (ULONG64)tib.StackLimit && addr < (ULONG64)tib.StackBase) {
            ULONG sc = 0; pNtQIT(t, 35, &sc, sizeof(sc), NULL);
            PWSTR desc = NULL; char name[64] = "";
            if (SUCCEEDED(GetThreadDescription(t, &desc)) && desc) { WideCharToMultiByte(CP_UTF8, 0, desc, -1, name, 64, NULL, NULL); LocalFree(desc); }
            fprintf(f, "owner tid %04lx suspend=%lu stack %p-%p name=[%s]\n", te.th32ThreadID, sc, tib.StackLimit, tib.StackBase, name);
        }
        CloseHandle(t);
    }
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}
