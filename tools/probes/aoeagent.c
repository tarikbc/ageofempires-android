// aoeagent: one long-running helper inside the Wine session that replaces the one-shot probes (peek, modbase, affin,
// tpause, tduty, blkread) started through `cmd /c x.bat`. It is a GUI-subsystem exe, so it opens no console window,
// and it is started once, so FEX does not load and JIT a new process for every measurement.
//
// Protocol (all files in D:\aoe\agent\, which is /sdcard/Download/aoe/agent on Android):
//   the host writes req.txt atomically (write + rename): one line "<id> <command> [args...]"
//   the agent polls every 50 ms, deletes req.txt, runs the command and writes rsp.txt atomically:
//   "id=<id> ok|err ...", then the command's lines, then "END".
//
// Commands (exe = RelicCardinal.exe unless `target` changed it; offsets and sizes are hex):
//   ping                               agent and game pid
//   target <exename>                   process the other commands act on
//   mod <module>                       base and size of a module in the target (e.g. libarm64ecfex.dll)
//   threads                            tid, start address (module+offset), user/kernel ms, affinity
//   peek <module|0> <off> <size>       hex dump of target memory at module base + off (module 0 = absolute address)
//   peekfile <module|0> <off> <size> <file>   the same bytes, raw, into a file
//   blkdump <fexrva> <file>            copy the BLKDUMP1 block dump of an analysis FEX build into a file
//   pause <exerva> <ms>                suspend the threads that start at exe+rva for ms (runs in the background)
//   duty <exerva> <off_ms> <on_ms> <total_ms>   suspend/resume those threads in a cycle (background)
//   affin <exerva> <mask> [others]     set the affinity of those threads (and of all other threads)
//   procaffin <mask>                   SetProcessAffinityMask(target, mask), then SetThreadAffinityMask on every
//                                      thread; prints the old process/system masks and how many calls succeeded
//   prio list                          tid, Windows thread name and priority of every thread
//   prio <name-prefix> <priority>      SetThreadPriority on the threads whose name starts with the prefix
//                                      (e.g. Game/Render 2 = THREAD_PRIORITY_HIGHEST)
//   sample <name-prefix|#tid> <count> [ms] [file]  suspend the first thread whose name starts with the prefix (or the
//                                      thread with that hex id) <count> times,
//                                      every [ms] (default 5), read its RIP and resume; prints samples per module,
//                                      the exe split at 0x143800000-0x145000000 (protection) and the top 4 KB pages;
//                                      with [file], also every sampled RIP, one hex value per line
//   callers <name-prefix|#tid> <count> <ms> <rip_lo> <rip_hi>  like sample, but only for samples whose RIP is in
//                                      [rip_lo, rip_hi): histogram of RCX (first argument) and of the exe return
//                                      addresses found in the first 0x800 bytes of the stack
//   quit
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIR "D:\\aoe\\agent\\"

typedef NTSTATUS (WINAPI *QIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef struct { NTSTATUS ExitStatus; PVOID TebBaseAddress; CLIENT_ID ClientId; ULONG_PTR AffinityMask; LONG Priority; LONG BasePriority; } TBI;

static QIT qit;
static char target[MAX_PATH] = "RelicCardinal.exe";
static FILE *out;

static DWORD find_pid(void)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, target)) pid = pe.th32ProcessID;
    CloseHandle(snap);
    return pid;
}

static HANDLE open_target(DWORD *pid_out)
{
    DWORD pid = find_pid();
    if (pid_out) *pid_out = pid;
    return pid ? OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_SET_INFORMATION, FALSE, pid) : NULL;
}

// module base by name; "exe" or the target name means the main module; "0" means absolute (base 0)
static ULONG64 module_base(HANDLE proc, const char *name, DWORD *size)
{
    if (!strcmp(name, "0")) return 0;
    HMODULE mods[1024];
    DWORD need = 0;
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL)) return 0;
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++) {
        char mname[MAX_PATH];
        MODULEINFO mi;
        if (!GetModuleBaseNameA(proc, mods[i], mname, sizeof(mname))) continue;
        if ((i == 0 && !_stricmp(name, "exe")) || !_stricmp(mname, name)) {
            if (size && GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) *size = mi.SizeOfImage;
            return (ULONG64)mods[i];
        }
    }
    return 0;
}

// describe an address as module+offset
static void describe(HANDLE proc, ULONG64 addr, char *buf, size_t len)
{
    HMODULE mods[1024];
    DWORD need = 0;
    snprintf(buf, len, "%llx", addr);
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL)) return;
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++) {
        MODULEINFO mi;
        char mname[MAX_PATH];
        if (!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
        ULONG64 b = (ULONG64)mi.lpBaseOfDll;
        if (addr >= b && addr < b + mi.SizeOfImage && GetModuleBaseNameA(proc, mods[i], mname, sizeof(mname))) {
            snprintf(buf, len, "%s+%llx", i == 0 ? "exe" : mname, addr - b);
            return;
        }
    }
}

// open the target's threads that start at exe+rva (all threads if rva == 0 and all != 0)
static int match_threads(DWORD pid, ULONG64 target_start, HANDLE *ths, DWORD *tids, int max, DWORD access)
{
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok && n < max; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_QUERY_INFORMATION | access, FALSE, te.th32ThreadID);
        if (!t) continue;
        PVOID start = NULL;
        qit(t, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), NULL);
        if ((ULONG64)start == target_start) {
            ths[n] = t;
            tids[n++] = te.th32ThreadID;
        } else {
            CloseHandle(t);
        }
    }
    CloseHandle(snap);
    return n;
}

typedef struct { HANDLE ths[16]; int n; DWORD off, on, total; } DUTY;

static DWORD WINAPI duty_worker(LPVOID p)
{
    DUTY *d = p;
    if (!d->on) {
        for (int i = 0; i < d->n; i++) SuspendThread(d->ths[i]);
        Sleep(d->off);
        for (int i = 0; i < d->n; i++) ResumeThread(d->ths[i]);
    } else {
        for (DWORD elapsed = 0; elapsed < d->total; elapsed += d->off + d->on) {
            for (int i = 0; i < d->n; i++) SuspendThread(d->ths[i]);
            Sleep(d->off);
            for (int i = 0; i < d->n; i++) ResumeThread(d->ths[i]);
            Sleep(d->on);
        }
    }
    for (int i = 0; i < d->n; i++) CloseHandle(d->ths[i]);
    free(d);
    return 0;
}

static void cmd_threads(HANDLE proc, DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t) continue;
        PVOID start = NULL;
        TBI tbi = { 0 };
        FILETIME c, e, k, u;
        char where[MAX_PATH + 32];
        qit(t, 9, &start, sizeof(start), NULL);
        qit(t, 0 /* ThreadBasicInformation */, &tbi, sizeof(tbi), NULL);
        GetThreadTimes(t, &c, &e, &k, &u);
        describe(proc, (ULONG64)start, where, sizeof(where));
        ULONG64 uk = ((ULONG64)u.dwHighDateTime << 32 | u.dwLowDateTime) / 10000;
        ULONG64 kk = ((ULONG64)k.dwHighDateTime << 32 | k.dwLowDateTime) / 10000;
        fprintf(out, "tid %04lx start=%s user=%llu kernel=%llu affinity=%llx\n", te.th32ThreadID, where, uk, kk,
                (ULONG64)tbi.AffinityMask);
        CloseHandle(t);
    }
    CloseHandle(snap);
}

static int run(char *line)
{
    char *argv[16];
    int argc = 0;
    for (char *tok = strtok(line, " \t\r\n"); tok && argc < 16; tok = strtok(NULL, " \t\r\n")) argv[argc++] = tok;
    if (argc < 2) { fprintf(out, "id=? err empty request\n"); return 1; }
    const char *id = argv[0], *cmd = argv[1];
    if (!strcmp(cmd, "quit")) { fprintf(out, "id=%s ok\n", id); return 0; }
    if (!strcmp(cmd, "target") && argc > 2) {
        snprintf(target, sizeof(target), "%s", argv[2]);
        fprintf(out, "id=%s ok target=%s\n", id, target);
        return 1;
    }
    DWORD pid = 0;
    HANDLE proc = open_target(&pid);
    if (!strcmp(cmd, "ping")) {
        fprintf(out, "id=%s ok agent=%lu target=%s pid=%lu\n", id, GetCurrentProcessId(), target, pid);
    } else if (!proc) {
        fprintf(out, "id=%s err %s not running or not readable (pid %lu)\n", id, target, pid);
    } else if (!strcmp(cmd, "mod") && argc > 2) {
        DWORD size = 0;
        ULONG64 base = module_base(proc, argv[2], &size);
        fprintf(out, base ? "id=%s ok base=%llx size=%lx\n" : "id=%s err module not loaded\n", id, base, size);
    } else if (!strcmp(cmd, "threads")) {
        fprintf(out, "id=%s ok pid=%lu\n", id, pid);
        cmd_threads(proc, pid);
    } else if ((!strcmp(cmd, "peek") && argc > 4) || (!strcmp(cmd, "peekfile") && argc > 5)) {
        ULONG64 base = module_base(proc, argv[2], NULL);
        ULONG64 addr = base + _strtoui64(argv[3], NULL, 16);
        SIZE_T size = (SIZE_T)_strtoui64(argv[4], NULL, 16), got = 0;
        unsigned char *buf = malloc(size ? size : 1);
        if (strcmp(argv[2], "0") && !base) {
            fprintf(out, "id=%s err module %s not loaded\n", id, argv[2]);
        } else if (!buf || !ReadProcessMemory(proc, (void *)addr, buf, size, &got)) {
            fprintf(out, "id=%s err read %llx failed %lu\n", id, addr, GetLastError());
        } else if (!strcmp(cmd, "peekfile")) {
            FILE *f = fopen(argv[5], "wb");
            if (f) { fwrite(buf, 1, got, f); fclose(f); }
            fprintf(out, f ? "id=%s ok addr=%llx bytes=%llu\n" : "id=%s err cannot write file\n", id, addr, (ULONG64)got);
        } else {
            fprintf(out, "id=%s ok addr=%llx bytes=%llu\n", id, addr, (ULONG64)got);
            for (SIZE_T i = 0; i < got; i += 32) {
                fprintf(out, "%llx:", addr + i);
                for (SIZE_T j = i; j < i + 32 && j < got; j++) fprintf(out, " %02x", buf[j]);
                fputc('\n', out);
            }
        }
        free(buf);
    } else if (!strcmp(cmd, "blkdump") && argc > 3) {
        ULONG64 base = module_base(proc, "libarm64ecfex.dll", NULL), hdr[6] = { 0 };
        SIZE_T got = 0;
        if (!base || !ReadProcessMemory(proc, (void *)(base + _strtoui64(argv[2], NULL, 16)), hdr, sizeof(hdr), &got) ||
            memcmp(&hdr[0], "BLKDUMP1", 8)) {
            fprintf(out, "id=%s err no BLKDUMP1 header at that RVA\n", id);
        } else {
            unsigned char *buf = malloc((size_t)hdr[3] + 1);
            FILE *f = fopen(argv[3], "wb");
            if (buf && f && (!hdr[3] || ReadProcessMemory(proc, (void *)hdr[1], buf, (SIZE_T)hdr[3], &got))) {
                fwrite(hdr, 8, 6, f);
                fwrite(buf, 1, (size_t)hdr[3], f);
                fprintf(out, "id=%s ok fex=%llx used=%llu records=%llu dropped=%llu\n", id, base, hdr[3], hdr[5], hdr[4]);
            } else {
                fprintf(out, "id=%s err dump read failed %lu\n", id, GetLastError());
            }
            if (f) fclose(f);
            free(buf);
        }
    } else if ((!strcmp(cmd, "pause") && argc > 3) || (!strcmp(cmd, "duty") && argc > 5)) {
        ULONG64 exe = module_base(proc, "exe", NULL);
        DUTY *d = calloc(1, sizeof(*d));
        DWORD tids[16];
        d->n = match_threads(pid, exe + _strtoui64(argv[2], NULL, 16), d->ths, tids, 16, THREAD_SUSPEND_RESUME);
        d->off = (DWORD)atoi(argv[3]);
        if (!strcmp(cmd, "duty")) { d->on = (DWORD)atoi(argv[4]); d->total = (DWORD)atoi(argv[5]); }
        fprintf(out, "id=%s ok threads=%d", id, d->n);
        for (int i = 0; i < d->n; i++) fprintf(out, " %04lx", tids[i]);
        fputc('\n', out);
        if (d->n) CloseHandle(CreateThread(NULL, 0, duty_worker, d, 0, NULL));
        else free(d);
    } else if (!strcmp(cmd, "affin") && argc > 3) {
        ULONG64 exe = module_base(proc, "exe", NULL), start = exe + _strtoui64(argv[2], NULL, 16);
        ULONG_PTR mask = (ULONG_PTR)_strtoui64(argv[3], NULL, 16), others = argc > 4 ? (ULONG_PTR)_strtoui64(argv[4], NULL, 16) : 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te = { sizeof(te) };
        int hit = 0, rest = 0;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION, FALSE, te.th32ThreadID);
            if (!t) continue;
            PVOID s = NULL;
            qit(t, 9, &s, sizeof(s), NULL);
            if ((ULONG64)s == start) { SetThreadAffinityMask(t, mask); hit++; }
            else if (others) { SetThreadAffinityMask(t, others); rest++; }
            CloseHandle(t);
        }
        CloseHandle(snap);
        fprintf(out, "id=%s ok matched=%d others=%d\n", id, hit, rest);
    } else if (!strcmp(cmd, "procaffin") && argc > 2) {
        DWORD_PTR mask = (DWORD_PTR)_strtoui64(argv[2], NULL, 16), pm = 0, sm = 0;
        GetProcessAffinityMask(proc, &pm, &sm);
        HANDLE ph = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
        BOOL pok = ph && SetProcessAffinityMask(ph, mask);
        DWORD perr = pok ? 0 : GetLastError();
        if (ph) CloseHandle(ph);
        int ok = 0, bad = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te = { sizeof(te) };
        for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION, FALSE, te.th32ThreadID);
            if (!t) { bad++; continue; }
            if (SetThreadAffinityMask(t, mask)) ok++; else bad++;
            CloseHandle(t);
        }
        CloseHandle(snap);
        fprintf(out, "id=%s ok process_mask_was=%llx system_mask=%llx set_process=%s(err %lu) threads_ok=%d failed=%d\n",
                id, (unsigned long long)pm, (unsigned long long)sm, pok ? "ok" : "failed", perr, ok, bad);
    } else if (!strcmp(cmd, "prio") && argc > 2) {
        typedef HRESULT (WINAPI *GTD)(HANDLE, PWSTR *);
        GTD gtd = (GTD)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetThreadDescription");
        BOOL list = !strcmp(argv[2], "list");
        int want = argc > 3 ? atoi(argv[3]) : 0, hit = 0;
        WCHAR prefix[128] = L"";
        if (!list) MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, prefix, 128);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te = { sizeof(te) };
        fprintf(out, "id=%s ok\n", id);
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION, FALSE, te.th32ThreadID);
            if (!t) continue;
            PWSTR name = NULL;
            if (gtd) gtd(t, &name);
            int before = GetThreadPriority(t);
            if (list) {
                fprintf(out, "tid %04lx prio %d name %ls\n", te.th32ThreadID, before, name ? name : L"");
            } else if (name && !wcsncmp(name, prefix, wcslen(prefix))) {
                BOOL set = SetThreadPriority(t, want);
                fprintf(out, "tid %04lx %ls prio %d -> %d %s\n", te.th32ThreadID, name, before, GetThreadPriority(t),
                        set ? "ok" : "failed");
                hit++;
            }
            if (name) LocalFree(name);
            CloseHandle(t);
        }
        CloseHandle(snap);
        if (!list) fprintf(out, "matched=%d\n", hit);
    } else if (!strcmp(cmd, "sample") && argc > 3) {
        typedef HRESULT (WINAPI *GTD)(HANDLE, PWSTR *);
        GTD gtd = (GTD)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetThreadDescription");
        int count = atoi(argv[3]), ms = argc > 4 ? atoi(argv[4]) : 5;
        WCHAR prefix[128];
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, prefix, 128);
        DWORD want_tid = argv[2][0] == '#' ? strtoul(argv[2] + 1, NULL, 16) : 0;  // "#<hex tid>" picks a thread by id
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0), th = NULL;
        THREADENTRY32 te = { sizeof(te) };
        for (BOOL ok = Thread32First(snap, &te); ok && !th; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!t) continue;
            PWSTR name = NULL;
            if (gtd) gtd(t, &name);
            if (want_tid ? te.th32ThreadID == want_tid : (name && !wcsncmp(name, prefix, wcslen(prefix)))) th = t;
            else CloseHandle(t);
            if (name) LocalFree(name);
        }
        CloseHandle(snap);
        if (!th) {
            fprintf(out, "id=%s err no thread named %s*\n", id, argv[2]);
        } else {
            HMODULE mods[512];
            DWORD need = 0;
            EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
            int nmod = need / sizeof(HMODULE);
            if (nmod > 512) nmod = 512;
            static ULONG64 mbase[512], msize[512];
            static char mname[512][64];
            static int mhits[512];
            for (int i = 0; i < nmod; i++) {
                MODULEINFO mi;
                GetModuleInformation(proc, mods[i], &mi, sizeof(mi));
                mbase[i] = (ULONG64)mi.lpBaseOfDll; msize[i] = mi.SizeOfImage; mhits[i] = 0;
                GetModuleBaseNameA(proc, mods[i], mname[i], sizeof(mname[i]));
            }
            ULONG64 *rips = malloc(sizeof(ULONG64) * (count ? count : 1));
            const char *dump = argc > 5 ? argv[5] : NULL;
            int got = 0, other = 0, prot = 0, exe = 0;
            for (int i = 0; i < count; i++) {
                if (SuspendThread(th) == (DWORD)-1) break;
                CONTEXT c;
                memset(&c, 0, sizeof(c));
                c.ContextFlags = CONTEXT_CONTROL;
                BOOL okc = GetThreadContext(th, &c);
                ResumeThread(th);
                if (okc) rips[got++] = c.Rip;
                Sleep(ms);
            }
            if (dump) {
                FILE *f = fopen(dump, "w");
                if (f) {
                    for (int i = 0; i < got; i++) fprintf(f, "%llx\n", rips[i]);
                    fclose(f);
                }
            }
            for (int i = 0; i < got; i++) {
                ULONG64 r = rips[i];
                int found = 0;
                for (int m = 0; m < nmod; m++) {
                    if (r >= mbase[m] && r < mbase[m] + msize[m]) {
                        mhits[m]++;
                        found = 1;
                        if (!_stricmp(mname[m], "RelicCardinal.exe")) {
                            exe++;
                            if (r >= 0x143800000ULL && r < 0x145000000ULL) prot++;
                        }
                        break;
                    }
                }
                if (!found) other++;
            }
            fprintf(out, "id=%s ok samples=%d thread=%04lx\n", id, got, te.th32ThreadID);
            for (int m = 0; m < nmod; m++)
                if (mhits[m]) fprintf(out, "module %-28s %5d %5.1f%%\n", mname[m], mhits[m], 100.0 * mhits[m] / got);
            fprintf(out, "module (no module: JIT code, heap) %5d %5.1f%%\n", other, 100.0 * other / (got ? got : 1));
            fprintf(out, "exe split: protection range %d, rest of exe %d\n", prot, exe - prot);
            // top 4 KB pages of the exe
            for (int k = 0; k < 15; k++) {
                ULONG64 best = 0;
                int bestn = 0;
                for (int i = 0; i < got; i++) {
                    ULONG64 page = rips[i] & ~0xfffULL;
                    if (!page || page < 0x140000000ULL || page >= 0x150000000ULL) continue;
                    int n = 0;
                    for (int j = 0; j < got; j++) if ((rips[j] & ~0xfffULL) == page) n++;
                    if (n > bestn) { bestn = n; best = page; }
                }
                if (!bestn) break;
                fprintf(out, "page exe+%llx %d\n", best - 0x140000000ULL, bestn);
                for (int j = 0; j < got; j++) if ((rips[j] & ~0xfffULL) == best) rips[j] = 0;
            }
            free(rips);
            CloseHandle(th);
        }
    } else if (!strcmp(cmd, "callers") && argc > 6) {
        typedef HRESULT (WINAPI *GTD)(HANDLE, PWSTR *);
        GTD gtd = (GTD)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetThreadDescription");
        int count = atoi(argv[3]), ms = atoi(argv[4]);
        ULONG64 lo = _strtoui64(argv[5], NULL, 16), hi = _strtoui64(argv[6], NULL, 16);
        WCHAR prefix[128];
        MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, prefix, 128);
        DWORD want_tid = argv[2][0] == '#' ? strtoul(argv[2] + 1, NULL, 16) : 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0), th = NULL;
        THREADENTRY32 te = { sizeof(te) };
        for (BOOL ok = Thread32First(snap, &te); ok && !th; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!t) continue;
            PWSTR name = NULL;
            if (gtd) gtd(t, &name);
            if (want_tid ? te.th32ThreadID == want_tid : (name && !wcsncmp(name, prefix, wcslen(prefix)))) th = t;
            else CloseHandle(t);
            if (name) LocalFree(name);
        }
        CloseHandle(snap);
        if (!th) {
            fprintf(out, "id=%s err no such thread\n", id);
        } else {
            enum { MAXK = 256 };
            static ULONG64 argk[MAXK], retk[MAXK];
            static int argn[MAXK], retn[MAXK];
            int nargs = 0, nrets = 0, hits = 0, total = 0;
            for (int i = 0; i < count; i++) {
                if (SuspendThread(th) == (DWORD)-1) break;
                CONTEXT c;
                memset(&c, 0, sizeof(c));
                c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                BOOL okc = GetThreadContext(th, &c);
                ULONG64 stack[0x100];
                SIZE_T got = 0;
                if (okc && c.Rip >= lo && c.Rip < hi)
                    ReadProcessMemory(proc, (LPCVOID)c.Rsp, stack, sizeof(stack), &got);
                ResumeThread(th);
                if (!okc) continue;
                total++;
                if (c.Rip < lo || c.Rip >= hi) { Sleep(ms); continue; }
                hits++;
                int k;
                for (k = 0; k < nargs && argk[k] != c.Rcx; k++) ;
                if (k < nargs) argn[k]++; else if (nargs < MAXK) { argk[nargs] = c.Rcx; argn[nargs++] = 1; }
                for (SIZE_T q = 0; q < got / 8; q++) {
                    ULONG64 v = stack[q];
                    if (v < 0x140001000ULL || v >= 0x146000000ULL) continue;
                    for (k = 0; k < nrets && retk[k] != v; k++) ;
                    if (k < nrets) retn[k]++; else if (nrets < MAXK) { retk[nrets] = v; retn[nrets++] = 1; }
                }
                Sleep(ms);
            }
            fprintf(out, "id=%s ok samples=%d in_range=%d\n", id, total, hits);
            for (int r = 0; r < 8; r++) {
                int b = -1;
                for (int k = 0; k < nargs; k++) if (argn[k] && (b < 0 || argn[k] > argn[b])) b = k;
                if (b < 0) break;
                fprintf(out, "rcx %llx %d\n", argk[b], argn[b]);
                argn[b] = 0;
            }
            for (int r = 0; r < 15; r++) {
                int b = -1;
                for (int k = 0; k < nrets; k++) if (retn[k] && (b < 0 || retn[k] > retn[b])) b = k;
                if (b < 0) break;
                fprintf(out, "ret exe+%llx %d\n", retk[b] - 0x140000000ULL, retn[b]);
                retn[b] = 0;
            }
            CloseHandle(th);
        }
    } else {
        fprintf(out, "id=%s err unknown command or missing arguments: %s\n", id, cmd);
    }
    if (proc) CloseHandle(proc);
    return 1;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)inst; (void)prev; (void)cmdline; (void)show;
    qit = (QIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    CreateDirectoryA("D:\\aoe", NULL);
    CreateDirectoryA(DIR, NULL);
    // one agent at a time: a second start exits at once
    CreateMutexA(NULL, TRUE, "Local\\aoeagent");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    DeleteFileA(DIR "req.txt");
    FILE *pidf = fopen(DIR "agent.pid", "w");
    if (pidf) { fprintf(pidf, "%lu\n", GetCurrentProcessId()); fclose(pidf); }
    for (;;) {
        Sleep(50);
        FILE *req = fopen(DIR "req.txt", "rb");
        if (!req) continue;
        char line[1024] = { 0 };
        size_t got = fread(line, 1, sizeof(line) - 1, req);
        fclose(req);
        DeleteFileA(DIR "req.txt");
        line[got] = 0;
        out = fopen(DIR "rsp.tmp", "w");
        if (!out) continue;
        int keep = run(line);
        fprintf(out, "END\n");
        fclose(out);
        MoveFileExA(DIR "rsp.tmp", DIR "rsp.txt", MOVEFILE_REPLACE_EXISTING);
        if (!keep) break;
    }
    DeleteFileA(DIR "agent.pid");
    return 0;
}
