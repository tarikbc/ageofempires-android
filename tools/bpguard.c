// bpguard: attach to a process as a debugger, plant int3 breakpoints at given VAs, and on each
// hit dump the thread context plus every return address on its stack (resolved to module+offset).
//
// The SuspendThread call sites (+0x49065a1, +0x490699d) are hit exactly twice in the whole binary,
// so breaking there lands directly on the protection's decision path.
//
// usage: bpguard.exe <exename> <outfile> <va_hex> [va_hex ...]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXBP 8

static FILE *out;
static HANDLE proc;
static HMODULE mods[2048];
static MODULEINFO minfo[2048];
static char mname[2048][64];
static DWORD nmods;

static ULONG64 bp_va[MAXBP];
static unsigned char bp_orig[MAXBP];
static int bp_armed[MAXBP];
static int nbp;
static int total_hits;

static void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fflush(out);
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
    if (m) logf("%s=%llx (%s+%llx)", tag, a, m, off);
    else logf("%s=%llx", tag, a);
}

static void load_modules(void)
{
    DWORD need = 0;
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL)) return;
    nmods = need / sizeof(HMODULE);
    if (nmods > 2048) nmods = 2048;
    for (DWORD i = 0; i < nmods; i++) {
        GetModuleInformation(proc, mods[i], &minfo[i], sizeof(minfo[i]));
        if (!GetModuleBaseNameA(proc, mods[i], mname[i], sizeof(mname[i]))) strcpy(mname[i], "?");
    }
}

static void write_byte(ULONG64 va, unsigned char b)
{
    DWORD old;
    SIZE_T w = 0;
    VirtualProtectEx(proc, (void *)va, 1, PAGE_EXECUTE_READWRITE, &old);
    WriteProcessMemory(proc, (void *)va, &b, 1, &w);
    VirtualProtectEx(proc, (void *)va, 1, old, &old);
    FlushInstructionCache(proc, (void *)va, 1);
}

static void arm(int i)
{
    write_byte(bp_va[i], 0xCC);
    bp_armed[i] = 1;
}

static void disarm(int i)
{
    write_byte(bp_va[i], bp_orig[i]);
    bp_armed[i] = 0;
}

static void dump_thread(HANDLE th, DWORD tid, ULONG64 bp)
{
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    logf("\n=== BP HIT at 0x%llx  tid=%04lx ===\n", bp, tid);
    if (!GetThreadContext(th, &ctx)) {
        logf("  GetThreadContext failed err=%lu\n", GetLastError());
        return;
    }
    logf("  rip="); print_addr("", ctx.Rip); logf("\n");
    logf("  rsp=%llx rbp=%llx rax=%llx rbx=%llx rcx=%llx rdx=%llx\n",
         ctx.Rsp, ctx.Rbp, ctx.Rax, ctx.Rbx, ctx.Rcx, ctx.Rdx);
    logf("  r8=%llx r9=%llx rdi=%llx rsi=%llx\n", ctx.R8, ctx.R9, ctx.Rdi, ctx.Rsi);

    ULONG64 stk[2048];
    SIZE_T got = 0;
    if (!ReadProcessMemory(proc, (void *)ctx.Rsp, stk, sizeof(stk), &got) || !got) {
        logf("  stack read failed err=%lu\n", GetLastError());
        return;
    }
    int n = (int)(got / 8);
    logf("  --- stack return addresses (module-resolved) ---\n");
    int shown = 0;
    for (int i = 0; i < n && shown < 60; i++) {
        ULONG64 off;
        const char *m = where(stk[i], &off);
        if (m) {
            logf("    [rsp+%04x] %016llx  %s+%llx\n", i * 8, stk[i], m, off);
            shown++;
        }
    }
    logf("  --- end (raw %d qwords scanned) ---\n", n);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: bpguard.exe <exename> <outfile> <va_hex> [va_hex...]\n");
        return 1;
    }
    out = fopen(argv[2], "w");
    if (!out) { fprintf(stderr, "cannot open outfile\n"); return 1; }
    nbp = argc - 3;
    if (nbp > MAXBP) nbp = MAXBP;
    for (int i = 0; i < nbp; i++) bp_va[i] = _strtoui64(argv[3 + i], NULL, 16);

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
    CloseHandle(snap);
    if (!pid) { logf("process not found\n"); fclose(out); return 1; }

    proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    logf("pid=0x%lx proc=%p bps=%d\n", pid, proc, nbp);
    if (!proc) { logf("OpenProcess failed err=%lu\n", GetLastError()); fclose(out); return 1; }
    load_modules();
    logf("modules=%lu\n", nmods);

    for (int i = 0; i < nbp; i++) {
        SIZE_T got = 0;
        if (!ReadProcessMemory(proc, (void *)bp_va[i], &bp_orig[i], 1, &got)) {
            logf("  cannot read bp %d at 0x%llx err=%lu\n", i, bp_va[i], GetLastError());
            continue;
        }
        logf("  bp %d: 0x%llx orig byte %02x\n", i, bp_va[i], bp_orig[i]);
        arm(i);
    }

    if (!DebugActiveProcess(pid)) {
        logf("DebugActiveProcess failed err=%lu\n", GetLastError());
        fclose(out);
        return 1;
    }
    DebugSetProcessKillOnExit(FALSE);
    logf("debugger attached\n");

    DWORD step_thread = 0;   /* tid we owe a re-arm after single-step */
    DEBUG_EVENT de;
    while (WaitForDebugEvent(&de, 120000)) {
        DWORD cont = DBG_CONTINUE;
        switch (de.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (de.u.CreateProcessInfo.hFile) CloseHandle(de.u.CreateProcessInfo.hFile);
            logf("[event] create process\n");
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            logf("[event] exit process code=%lu\n", de.u.ExitProcess.dwExitCode);
            goto done;
        case CREATE_THREAD_DEBUG_EVENT:
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (de.u.LoadDll.hFile) CloseHandle(de.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            EXCEPTION_RECORD *er = &de.u.Exception.ExceptionRecord;
            ULONG64 addr = (ULONG64)er->ExceptionAddress;
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT) {
                int hit = -1;
                for (int i = 0; i < nbp; i++)
                    if (bp_armed[i] && addr == bp_va[i] + 1) hit = i;
                if (hit >= 0) {
                    total_hits++;
                    HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, de.dwThreadId);
                    dump_thread(th, de.dwThreadId, bp_va[hit]);
                    if (th) CloseHandle(th);
                    disarm(hit);
                    step_thread = de.dwThreadId;   /* re-arm after the single step */
                    if (total_hits >= 12) { logf("\n(hit cap reached)\n"); goto done; }
                } else {
                    /* initial debug breakpoint or someone else's int3 */
                    if (de.u.Exception.dwFirstChance) cont = DBG_CONTINUE;
                }
            } else if (er->ExceptionCode == EXCEPTION_SINGLE_STEP) {
                if (de.dwThreadId == step_thread) {
                    for (int i = 0; i < nbp; i++)
                        if (!bp_armed[i] && bp_va[i] != 0) arm(i);
                    step_thread = 0;
                }
            } else {
                /* not ours: let the app handle it */
                cont = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }
        default:
            break;
        }
        if (!ContinueDebugEvent(de.dwProcessId, de.dwThreadId, cont)) {
            logf("ContinueDebugEvent failed err=%lu\n", GetLastError());
            break;
        }
    }
done:
    for (int i = 0; i < nbp; i++) if (bp_armed[i]) disarm(i);
    logf("\ntotal_hits=%d\nEND\n", total_hits);
    fclose(out);
    return 0;
}
