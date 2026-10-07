// stkdump: copy one thread's whole committed stack (TEB StackLimit..StackBase) out of a process, for offline search
// of stale return addresses. The thread may be suspended; nothing is changed.
// usage: stkdump.exe <exename> <tid_hex> <outfile>
// Output: u64 StackLimit, u64 StackBase, u64 Rsp (from GetThreadContext, 0 if unavailable), then the stack bytes.
// Build: x86_64-w64-mingw32-gcc -O1 -static -o stkdump.exe stkdump.c -lntdll
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { LONG ExitStatus; PVOID TebBaseAddress; ULONG_PTR cid[2]; ULONG_PTR Aff; LONG Pri, BasePri; } TBI;
typedef NTSTATUS (NTAPI *NtQIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);

int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    char logname[MAX_PATH];
    snprintf(logname, sizeof(logname), "%s.log", argv[3]);
    FILE *log = fopen(logname, "w");
    if (!log) return 1;
    DWORD tid = strtoul(argv[2], NULL, 16), pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    HANDLE thr = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, tid);
    if (!proc || !thr) { fprintf(log, "open failed pid=%lu tid=%lx err=%lu\n", pid, tid, GetLastError()); fclose(log); return 1; }
    TBI tbi;
    NtQIT q = (NtQIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    if (q(thr, 0 /* ThreadBasicInformation */, &tbi, sizeof(tbi), NULL)) { fprintf(log, "query failed\n"); fclose(log); return 1; }
    ULONG64 tib[3] = { 0 }; // ExceptionList, StackBase, StackLimit
    SIZE_T got = 0;
    ReadProcessMemory(proc, tbi.TebBaseAddress, tib, sizeof(tib), &got);
    ULONG64 base = tib[1], limit = tib[2], rsp = 0;
    CONTEXT ctx = { 0 };
    ctx.ContextFlags = CONTEXT_CONTROL;
    if (GetThreadContext(thr, &ctx)) rsp = ctx.Rsp;
    if (!base || base <= limit || base - limit > (256 << 20)) { fprintf(log, "bad stack %llx..%llx\n", limit, base); fclose(log); return 1; }
    unsigned char *buf = calloc(1, (size_t)(base - limit));
    SIZE_T total = 0;
    for (ULONG64 a = limit; a < base; a += 0x1000) {
        SIZE_T n = 0;
        if (ReadProcessMemory(proc, (void *)a, buf + (a - limit), 0x1000, &n)) total += n;
    }
    FILE *out = fopen(argv[3], "wb");
    fwrite(&limit, 8, 1, out);
    fwrite(&base, 8, 1, out);
    fwrite(&rsp, 8, 1, out);
    fwrite(buf, 1, (size_t)(base - limit), out);
    fclose(out);
    fprintf(log, "pid=%lu tid=%lx teb=%p stack %llx..%llx rsp=%llx read=%llu\n", pid, tid, tbi.TebBaseAddress, limit, base, rsp,
            (unsigned long long)total);
    fclose(log);
    return 0;
}
