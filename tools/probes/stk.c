// stk: for every thread, show the suspend count and the state of the top 8 stack pages, and flag
// threads whose live frames (at or above RSP) are no longer committed.
// usage: stk.exe   (target RelicCardinal.exe, output D:\stk.txt)
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
typedef struct { LONG ExitStatus; PVOID TebBaseAddress; ULONG_PTR cid[2]; ULONG_PTR Aff; LONG Pri, BasePri; } TBI;
int main(void)
{
    FILE *f = fopen("D:\\stk.txt", "w");
    NTSTATUS (WINAPI *pNtQIT)(HANDLE, int, PVOID, ULONG, PULONG) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, "RelicCardinal.exe")) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    int bad = 0, total = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        TBI tbi = { 0 };
        pNtQIT(t, 0, &tbi, sizeof(tbi), NULL);
        NT_TIB tib; SIZE_T got;
        if (!ReadProcessMemory(proc, tbi.TebBaseAddress, &tib, sizeof(tib), &got)) { CloseHandle(t); continue; }
        ULONG sc = 0; pNtQIT(t, 35, &sc, sizeof(sc), NULL);
        CONTEXT ctx = { 0 }; ctx.ContextFlags = CONTEXT_CONTROL;
        ULONG64 rsp = 0;
        if (sc && GetThreadContext(t, &ctx)) rsp = ctx.Rsp;
        char st[64] = "";
        int decommitted = 0;
        for (int k = 1; k <= 8; k++) {
            MEMORY_BASIC_INFORMATION m;
            ULONG64 a = (ULONG64)tib.StackBase - k * 0x1000;
            if (VirtualQueryEx(proc, (void *)a, &m, sizeof(m))) {
                st[k - 1] = m.State == MEM_COMMIT ? (m.Protect & PAGE_GUARD ? 'g' : 'C') : (m.State == MEM_RESERVE ? 'r' : 'f');
                if (m.State != MEM_COMMIT && rsp && a >= (rsp & ~0xfffull)) decommitted = 1;
            }
        }
        total++;
        if (decommitted) bad++;
        fprintf(f, "tid %04lx suspend=%lu base=%p rsp=%llx top8=[%s]%s\n", te.th32ThreadID, sc, tib.StackBase, rsp, st, decommitted ? " LIVE-FRAMES-DECOMMITTED" : "");
        CloseHandle(t);
    }
    fprintf(f, "threads=%d with decommitted live frames=%d\nEND\n", total, bad);
    fclose(f);
    return 0;
}
