// protmon: watch a process's memory-protection map over time.
// FEX's SMC trap (SMCChecks=mtrack) flips guest RWX pages to PAGE_EXECUTE_READ while
// armed and back to PAGE_EXECUTE_READWRITE when a write faults. Those transitions are
// visible from another process, and are absent under SMCChecks=none.
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

static DWORD find_pid(const char *name)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = { sizeof(pe) }; DWORD pid = 0;
    if (Process32First(s, &pe)) do {
        if (!_stricmp(pe.szExeFile, name)) { pid = pe.th32ProcessID; break; }
    } while (Process32Next(s, &pe));
    CloseHandle(s); return pid;
}
static const char *pname(ULONG p)
{
    static char b[64]; b[0] = 0;
    if (p & PAGE_GUARD)        strcat(b, "GUARD|");
    if (p & PAGE_NOCACHE)      strcat(b, "NOCACHE|");
    if (p & PAGE_WRITECOMBINE) strcat(b, "WC|");
    switch (p & 0xff) {
    case PAGE_NOACCESS:          strcat(b, "NOACCESS"); break;
    case PAGE_READONLY:          strcat(b, "R"); break;
    case PAGE_READWRITE:         strcat(b, "RW"); break;
    case PAGE_WRITECOPY:         strcat(b, "WC"); break;
    case PAGE_EXECUTE:           strcat(b, "X"); break;
    case PAGE_EXECUTE_READ:      strcat(b, "RX"); break;
    case PAGE_EXECUTE_READWRITE: strcat(b, "RWX"); break;
    case PAGE_EXECUTE_WRITECOPY: strcat(b, "XWC"); break;
    default: sprintf(b + strlen(b), "?0x%lx", p & 0xff);
    }
    return b;
}
typedef struct { ULONG64 base; ULONG64 size; ULONG prot; } REG;
#define MAXR 4096
int main(int argc, char **argv)
{
    const char *target = argc > 1 ? argv[1] : "RelicCardinal.exe";
    int samples = argc > 2 ? atoi(argv[2]) : 40;
    FILE *out = fopen("D:\\aoe\\protmon.txt", "w");
    if (!out) return 1;
    DWORD pid = find_pid(target);
    fprintf(out, "protmon: %s pid=%lu samples=%d\n\n", target, pid, samples);
    if (!pid) { fclose(out); return 0; }
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) { fprintf(out, "OpenProcess err=%lu\n", GetLastError()); fclose(out); return 0; }

    static REG prev[MAXR], cur[MAXR];
    int nprev = 0;
    for (int s = 0; s < samples; s++) {
        int n = 0; ULONG64 addr = 0; MEMORY_BASIC_INFORMATION m;
        while (n < MAXR && VirtualQueryEx(h, (LPCVOID)addr, &m, sizeof(m))) {
            if (m.State == MEM_COMMIT) {
                cur[n].base = (ULONG64)m.BaseAddress; cur[n].size = m.RegionSize; cur[n].prot = m.Protect;
                n++;
            }
            ULONG64 next = (ULONG64)m.BaseAddress + m.RegionSize;
            if (next <= addr) break;
            addr = next;
        }
        int changes = 0;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < nprev; j++) {
                if (prev[j].base == cur[i].base && prev[j].prot != cur[i].prot) {
                    if (!(cur[i].prot & PAGE_GUARD) && !(prev[j].prot & PAGE_GUARD)) {
                        fprintf(out, "  t=%2d  0x%llx  %s -> %s   (size 0x%llx)\n",
                                s, cur[i].base, pname(prev[j].prot), pname(cur[i].prot), cur[i].size);
                        changes++;
                    }
                }
            }
        }
        int rwx = 0, rx = 0;
        for (int i = 0; i < n; i++) {
            ULONG p = cur[i].prot & 0xff;
            if (p == PAGE_EXECUTE_READWRITE) rwx++;
            if (p == PAGE_EXECUTE_READ) rx++;
        }
        if (s == 0 || changes || s % 5 == 0)
            fprintf(out, "sample %2d: regions=%d RWX=%d RX=%d protection-changes=%d\n", s, n, rwx, rx, changes);
        fflush(out);
        memcpy(prev, cur, sizeof(REG) * n); nprev = n;
        Sleep(3000);
    }
    fprintf(out, "\nEND\n"); fclose(out); CloseHandle(h);
    return 0;
}
