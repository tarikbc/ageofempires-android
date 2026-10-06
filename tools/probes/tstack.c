// tstack: for the busiest thread of a process, dump code pointers found on its stack (no suspend needed).
// usage: tstack.exe [exename] [outfile]   defaults: RelicCardinal.exe D:\tstack.txt
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>

static FILE *out;
#define LOG(...) do { fprintf(out, __VA_ARGS__); fflush(out); } while (0)

static HANDLE proc;
static HMODULE mods[1024];
static MODULEINFO minfo[1024];
static char mname[1024][64];
static DWORD nmods;

typedef struct { NTSTATUS ExitStatus; PVOID TebBaseAddress; CLIENT_ID ClientId; ULONG_PTR AffinityMask; LONG Priority; LONG BasePriority; } TBI;

static const char *where(ULONG64 a, ULONG64 *off)
{
    for (DWORD i = 0; i < nmods; i++) {
        ULONG64 b = (ULONG64)minfo[i].lpBaseOfDll;
        if (a >= b && a < b + minfo[i].SizeOfImage) { *off = a - b; return mname[i]; }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *exe = argc > 1 ? argv[1] : "RelicCardinal.exe";
    out = fopen(argc > 2 ? argv[2] : "D:\\tstack.txt", "w");
    NTSTATUS (WINAPI *pNtQIT)(HANDLE, int, PVOID, ULONG, PULONG) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, exe)) pid = pe.th32ProcessID;
    CloseHandle(snap);
    proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { LOG("open failed\n"); return 1; }
    DWORD need = 0;
    EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
    nmods = need / sizeof(HMODULE);
    for (DWORD i = 0; i < nmods; i++) {
        GetModuleInformation(proc, mods[i], &minfo[i], sizeof(minfo[i]));
        if (!GetModuleBaseNameA(proc, mods[i], mname[i], sizeof(mname[i]))) strcpy(mname[i], "?");
    }

    // rank threads by user time
    DWORD tids[512], nt = 0;
    ULONG64 ut[512];
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && nt < 512) tids[nt++] = te.th32ThreadID;
    CloseHandle(snap);
    int best = -1, second = -1;
    for (DWORD i = 0; i < nt; i++) {
        HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tids[i]);
        FILETIME c, e, k, u;
        ut[i] = 0;
        if (t && GetThreadTimes(t, &c, &e, &k, &u)) ut[i] = ((ULONG64)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10000;
        if (t) CloseHandle(t);
        if (best < 0 || ut[i] > ut[best]) { second = best; best = i; }
        else if (second < 0 || ut[i] > ut[second]) second = i;
    }
    int pick[2] = { best, second };
    for (int p = 0; p < 2; p++) {
        int i = pick[p];
        if (i < 0) continue;
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, tids[i]);
        TBI tbi = { 0 };
        NTSTATUS st = pNtQIT(t, 0 /* ThreadBasicInformation */, &tbi, sizeof(tbi), NULL);
        PVOID start = NULL;
        pNtQIT(t, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), NULL);
        ULONG64 off;
        const char *m = where((ULONG64)start, &off);
        LOG("\n== tid %04lx user=%llums teb=%p st=%lx start=%p %s+%llx\n", tids[i], ut[i], tbi.TebBaseAddress, st, start, m ? m : "?", m ? off : 0);
        NT_TIB tib;
        SIZE_T got;
        if (!ReadProcessMemory(proc, tbi.TebBaseAddress, &tib, sizeof(tib), &got)) { LOG("  teb read err=%lu\n", GetLastError()); continue; }
        LOG("  stack base=%p limit=%p\n", tib.StackBase, tib.StackLimit);
        ULONG64 lo = (ULONG64)tib.StackLimit, hi = (ULONG64)tib.StackBase;
        if (hi - lo > 0x200000) lo = hi - 0x200000;
        SIZE_T n = (hi - lo) / 8;
        ULONG64 *buf = calloc(n, 8);
        for (ULONG64 a = lo; a < hi; a += 0x1000)
            ReadProcessMemory(proc, (void *)a, (char *)buf + (a - lo), 0x1000, &got);
        // first non-zero qword from the low end approximates the deepest stack use
        SIZE_T firstnz = 0;
        while (firstnz < n && !buf[firstnz]) firstnz++;
        LOG("  deepest non-zero at %llx (depth %llu bytes)\n", lo + firstnz * 8, (hi - lo) - firstnz * 8);
        int shown = 0;
        for (SIZE_T q = firstnz; q < n && shown < 120; q++) {
            const char *mm = where(buf[q], &off);
            if (mm) { LOG("  [%llx] %llx %s+%llx\n", lo + q * 8, buf[q], mm, off); shown++; }
        }
        // raw dump of the 512 bytes at the deepest point
        LOG("  raw@deepest:\n");
        for (SIZE_T q = firstnz; q < firstnz + 64 && q < n; q += 4)
            LOG("   %llx: %016llx %016llx %016llx %016llx\n", lo + q * 8, buf[q], buf[q + 1], buf[q + 2], buf[q + 3]);
        free(buf);
        CloseHandle(t);
    }
    LOG("END\n");
    return 0;
}
