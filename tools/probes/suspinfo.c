// suspinfo: list every thread of a process with its suspend count; for threads that are already
// suspended, read their context without suspending anything else. Also dumps one memory address.
// usage: suspinfo.exe [exename] [outfile] [hexaddr]   defaults: RelicCardinal.exe D:\si.txt
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *out;
#define LOG(...) do { fprintf(out, __VA_ARGS__); fflush(out); } while (0)

static HMODULE mods[1024];
static MODULEINFO minfo[1024];
static char mname[1024][64];
static DWORD nmods;

static const char *where(ULONG64 a, ULONG64 *off)
{
    for (DWORD i = 0; i < nmods; i++) {
        ULONG64 b = (ULONG64)minfo[i].lpBaseOfDll;
        if (a >= b && a < b + minfo[i].SizeOfImage) { *off = a - b; return mname[i]; }
    }
    *off = a;
    return "?";
}

int main(int argc, char **argv)
{
    const char *exe = argc > 1 ? argv[1] : "RelicCardinal.exe";
    out = fopen(argc > 2 ? argv[2] : "D:\\si.txt", "w");
    ULONG64 dumpaddr = argc > 3 ? _strtoui64(argv[3], NULL, 16) : 0;
    NTSTATUS (WINAPI *pNtQIT)(HANDLE, int, PVOID, ULONG, PULONG) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, exe)) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { LOG("open failed\n"); return 1; }
    DWORD need = 0;
    EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
    nmods = need / sizeof(HMODULE);
    for (DWORD i = 0; i < nmods; i++) {
        GetModuleInformation(proc, mods[i], &minfo[i], sizeof(minfo[i]));
        if (!GetModuleBaseNameA(proc, mods[i], mname[i], sizeof(mname[i]))) strcpy(mname[i], "?");
    }
    if (dumpaddr) {
        ULONG64 v[4] = { 0 };
        SIZE_T got = 0;
        BOOL ok = ReadProcessMemory(proc, (void *)dumpaddr, v, sizeof(v), &got);
        LOG("dump %llx ok=%d err=%lu: %016llx %016llx %016llx %016llx\n", dumpaddr, ok, ok ? 0 : GetLastError(), v[0], v[1], v[2], v[3]);
    }
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        if (!t) { LOG("tid %04lx open err=%lu\n", te.th32ThreadID, GetLastError()); continue; }
        ULONG sc = 0xffffffff;
        pNtQIT(t, 35 /* ThreadSuspendCount */, &sc, sizeof(sc), NULL);
        FILETIME c, e, k, u;
        ULONG64 ut = 0;
        if (GetThreadTimes(t, &c, &e, &k, &u)) ut = ((ULONG64)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10000;
        PWSTR desc = NULL;
        char name[64] = "";
        if (SUCCEEDED(GetThreadDescription(t, &desc)) && desc) { WideCharToMultiByte(CP_UTF8, 0, desc, -1, name, sizeof(name), NULL, NULL); LocalFree(desc); }
        PVOID start = NULL;
        pNtQIT(t, 9, &start, sizeof(start), NULL);
        ULONG64 off;
        const char *sm = where((ULONG64)start, &off);
        LOG("tid %04lx suspend=%lu user=%llums start=%s+%llx name=[%s]\n", te.th32ThreadID, sc, ut, sm, off, name);
        if (sc != 0 && sc != 0xffffffff) {
            CONTEXT ctx = { 0 };
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(t, &ctx)) {
                const char *m = where(ctx.Rip, &off);
                LOG("   rip=%llx (%s+%llx) rsp=%llx rax=%llx rcx=%llx rdx=%llx\n", ctx.Rip, m, off, ctx.Rsp, ctx.Rax, ctx.Rcx, ctx.Rdx);
            } else LOG("   getctx err=%lu\n", GetLastError());
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    LOG("END\n");
    return 0;
}
