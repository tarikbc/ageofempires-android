// stkscan: for every thread, scan the stack from its current SP (suspended threads) or the whole
// committed stack, and list return addresses that fall inside a given ntdll RVA range.
// usage: stkscan.exe [exename] [outfile] [lo_rva] [hi_rva]
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *out;
#define LOG(...) do { fprintf(out, __VA_ARGS__); fflush(out); } while (0)

typedef struct { LONG ExitStatus; PVOID TebBaseAddress; ULONG_PTR cid[2]; ULONG_PTR Aff; LONG Pri, BasePri; } TBI;

int main(int argc, char **argv)
{
    const char *exe = argc > 1 ? argv[1] : "RelicCardinal.exe";
    out = fopen(argc > 2 ? argv[2] : "D:\\ss.txt", "w");
    ULONG64 lo = argc > 3 ? _strtoui64(argv[3], NULL, 16) : 0xb491c;
    ULONG64 hi = argc > 4 ? _strtoui64(argv[4], NULL, 16) : 0xce640;
    NTSTATUS (WINAPI *pNtQIT)(HANDLE, int, PVOID, ULONG, PULONG) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, exe)) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { LOG("open failed\n"); return 1; }
    HMODULE mods[1024];
    DWORD need = 0;
    char name[64];
    ULONG64 nt = 0;
    EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++)
        if (GetModuleBaseNameA(proc, mods[i], name, sizeof(name)) && !_stricmp(name, "ntdll.dll")) nt = (ULONG64)mods[i];
    LOG("ntdll=%llx range %llx-%llx\n", nt, nt + lo, nt + hi);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    ULONG64 *buf = malloc(0x200000);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (!t) continue;
        ULONG sc = 0;
        pNtQIT(t, 35, &sc, sizeof(sc), NULL);
        TBI tbi = { 0 };
        pNtQIT(t, 0, &tbi, sizeof(tbi), NULL);
        NT_TIB tib;
        SIZE_T got;
        if (!ReadProcessMemory(proc, tbi.TebBaseAddress, &tib, sizeof(tib), &got)) { CloseHandle(t); continue; }
        ULONG64 sp = (ULONG64)tib.StackLimit, base = (ULONG64)tib.StackBase;
        if (sc) {
            CONTEXT ctx = { 0 };
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(t, &ctx) && ctx.Rsp > sp && ctx.Rsp < base) sp = ctx.Rsp & ~7ull;
        }
        if (base - sp > 0x200000) sp = base - 0x200000;
        // read page by page
        SIZE_T n = (base - sp) / 8;
        for (ULONG64 a = sp & ~0xfffull; a < base; a += 0x1000) {
            ULONG64 from = a < sp ? sp : a, to = a + 0x1000 < base ? a + 0x1000 : base;
            ReadProcessMemory(proc, (void *)from, (char *)buf + (from - sp), to - from, &got);
        }
        int hits = 0;
        for (SIZE_T q = 0; q < n && hits < 6; q++) {
            ULONG64 v = buf[q];
            if (v >= nt + lo && v < nt + hi) {
                if (!hits) LOG("tid %04lx suspend=%lu sp=%llx:", te.th32ThreadID, sc, sp);
                LOG(" [+%llx]=ntdll+%llx", q * 8, v - nt);
                hits++;
            }
        }
        if (hits) LOG("\n");
        CloseHandle(t);
    }
    LOG("END\n");
    return 0;
}
