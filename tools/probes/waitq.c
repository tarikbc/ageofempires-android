// waitq: dump Wine ntdll's RtlWaitOnAddress hash table (256 buckets of {list head, spinlock})
// from a target process, to find a spinlock that stays held while threads spin on it.
// usage: waitq.exe [exename] [outfile] [table_rva]   defaults: RelicCardinal.exe D:\wq.txt 156e30
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *out;
#define LOG(...) do { fprintf(out, __VA_ARGS__); fflush(out); } while (0)

struct bucket { ULONG64 next, prev; LONG lock; LONG pad; };

int main(int argc, char **argv)
{
    const char *exe = argc > 1 ? argv[1] : "RelicCardinal.exe";
    out = fopen(argc > 2 ? argv[2] : "D:\\wq.txt", "w");
    ULONG64 rva = argc > 3 ? _strtoui64(argv[3], NULL, 16) : 0x156e30;
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
    ULONG64 ntbase = 0;
    EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++)
        if (GetModuleBaseNameA(proc, mods[i], name, sizeof(name)) && !_stricmp(name, "ntdll.dll")) ntbase = (ULONG64)mods[i];
    LOG("pid=0x%lx ntdll=%llx table=%llx (own ntdll=%p)\n", pid, ntbase, ntbase + rva, GetModuleHandleA("ntdll.dll"));
    ULONG64 table = ntbase + rva;
    for (int pass = 0; pass < 3; pass++) {
        struct bucket b[256];
        SIZE_T got = 0;
        if (!ReadProcessMemory(proc, (void *)table, b, sizeof(b), &got)) { LOG("read err=%lu\n", GetLastError()); return 1; }
        int held = 0, nonempty = 0;
        for (int i = 0; i < 256; i++) {
            ULONG64 head = table + i * sizeof(struct bucket);
            int empty = (b[i].next == head || b[i].next == 0);
            if (b[i].lock) held++;
            if (!empty) nonempty++;
            if (b[i].lock || (!empty && pass == 0)) {
                LOG("pass %d bucket %3d lock=%08lx next=%llx prev=%llx\n", pass, i, b[i].lock, b[i].next, b[i].prev);
                // walk up to 8 waiters: struct futex_entry { list entry; const void *addr; DWORD tid; }
                ULONG64 e = b[i].next;
                for (int n = 0; n < 8 && e && e != head; n++) {
                    ULONG64 ent[4] = { 0 };
                    if (!ReadProcessMemory(proc, (void *)e, ent, sizeof(ent), &got)) break;
                    LOG("    waiter %llx addr=%llx tid=%04llx\n", e, ent[2], ent[3] & 0xffffffff);
                    e = ent[0];
                }
            }
        }
        LOG("pass %d: held=%d nonempty=%d\n", pass, held, nonempty);
        Sleep(1000);
    }
    LOG("END\n");
    return 0;
}
