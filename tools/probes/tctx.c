// tctx: sample the x64 context of every thread in a process (to find a spinning thread).
// usage: tctx.exe [exename] [outfile]   defaults: RelicCardinal.exe D:\tctx.txt
#include <windows.h>
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

static void load_modules(void)
{
    DWORD need = 0;
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL)) { LOG("EnumProcessModulesEx err=%lu\n", GetLastError()); return; }
    nmods = need / sizeof(HMODULE);
    if (nmods > 1024) nmods = 1024;
    for (DWORD i = 0; i < nmods; i++) {
        GetModuleInformation(proc, mods[i], &minfo[i], sizeof(minfo[i]));
        if (!GetModuleBaseNameA(proc, mods[i], mname[i], sizeof(mname[i]))) strcpy(mname[i], "?");
    }
    LOG("modules=%lu\n", nmods);
}

static const char *where(ULONG64 a, ULONG64 *off)
{
    for (DWORD i = 0; i < nmods; i++) {
        ULONG64 b = (ULONG64)minfo[i].lpBaseOfDll;
        if (a >= b && a < b + minfo[i].SizeOfImage) { *off = a - b; return mname[i]; }
    }
    *off = a;
    return NULL;
}

static void print_addr(const char *tag, ULONG64 a)
{
    ULONG64 off;
    const char *m = where(a, &off);
    if (m) LOG("%s=%llx (%s+%llx)", tag, a, m, off);
    else LOG("%s=%llx", tag, a);
}

int main(int argc, char **argv)
{
    const char *exe = argc > 1 ? argv[1] : "RelicCardinal.exe";
    out = fopen(argc > 2 ? argv[2] : "D:\\tctx.txt", "w");
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, exe)) pid = pe.th32ProcessID;
    CloseHandle(snap);
    proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    LOG("pid=0x%lx proc=%p\n", pid, proc);
    if (!proc) return 1;
    load_modules();

    DWORD tids[512], nt = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && nt < 512) tids[nt++] = te.th32ThreadID;
    CloseHandle(snap);
    LOG("threads=%lu\n", nt);

    // rank by user time, sample the busiest ones several times
    ULONG64 utime[512] = {0};
    for (DWORD i = 0; i < nt; i++) {
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, tids[i]);
        FILETIME c, e, k, u;
        if (t && GetThreadTimes(t, &c, &e, &k, &u)) utime[i] = ((ULONG64)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10000;
        if (t) CloseHandle(t);
    }
    for (DWORD i = 0; i < nt; i++) {
        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, tids[i]);
        if (!t) { LOG("tid %04lx open err=%lu\n", tids[i], GetLastError()); continue; }
        PWSTR desc = NULL;
        char name[64] = "";
        if (SUCCEEDED(GetThreadDescription(t, &desc)) && desc) { WideCharToMultiByte(CP_UTF8, 0, desc, -1, name, sizeof(name), NULL, NULL); LocalFree(desc); }
        LOG("\n== tid %04lx user=%llums name=[%s]\n", tids[i], utime[i], name);
        int samples = utime[i] > 20000 ? 6 : 1;
        for (int s = 0; s < samples; s++) {
            if (s) Sleep(150);
            if (SuspendThread(t) == (DWORD)-1) { LOG("  suspend err=%lu\n", GetLastError()); break; }
            CONTEXT ctx = { 0 };
            ctx.ContextFlags = CONTEXT_FULL;
            BOOL ok = GetThreadContext(t, &ctx);
            ResumeThread(t);
            if (!ok) { LOG("  getctx err=%lu\n", GetLastError()); break; }
            LOG("  ");
            print_addr("rip", ctx.Rip);
            LOG(" rsp=%llx rax=%llx rcx=%llx rdx=%llx rbx=%llx\n", ctx.Rsp, ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.Rbx);
            if (s == 0 && samples > 1) {
                LOG("  r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx rsi=%llx rdi=%llx rbp=%llx efl=%lx\n",
                    ctx.R8, ctx.R9, ctx.R10, ctx.R11, ctx.R12, ctx.R13, ctx.R14, ctx.R15, ctx.Rsi, ctx.Rdi, ctx.Rbp, ctx.EFlags);
                unsigned char code[64];
                SIZE_T got = 0;
                if (ReadProcessMemory(proc, (void *)(ctx.Rip - 16), code, sizeof(code), &got)) {
                    LOG("  code@rip-16:");
                    for (int b = 0; b < 64; b++) LOG(" %02x", code[b]);
                    LOG("\n");
                }
                ULONG64 stk[256];
                if (ReadProcessMemory(proc, (void *)ctx.Rsp, stk, sizeof(stk), &got)) {
                    for (int q = 0; q < 256; q++) {
                        ULONG64 off;
                        const char *m = where(stk[q], &off);
                        if (m) LOG("  [rsp+%03x] %llx %s+%llx\n", q * 8, stk[q], m, off);
                    }
                }
            }
        }
        CloseHandle(t);
    }
    LOG("END\n");
    return 0;
}
