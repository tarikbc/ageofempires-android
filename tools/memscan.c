// memscan: scan a target process's committed memory for byte patterns and dump context.
// usage: memscan.exe <exename> <outfile> <pattern> [pattern...]
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;

static int readable(DWORD p)
{
    if (p & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                 PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

static void dump_context(HANDLE proc, ULONG64 va)
{
    ULONG64 cs = va > 0x100 ? va - 0x100 : 0;
    SIZE_T cl = 0x300, got = 0;
    unsigned char ctx[0x400];
    if (!ReadProcessMemory(proc, (void *)cs, ctx, cl, &got) || !got) return;
    fprintf(out, "\n=== HIT at 0x%llx (context 0x%llx) ===\n", va, cs);
    for (SIZE_T k = 0; k < got; k++) {
        unsigned char c = ctx[k];
        fputc((c >= 32 && c < 127) ? c : '.', out);
    }
    fprintf(out, "\n-- hex --\n");
    for (SIZE_T k = 0; k < got; k += 16) {
        fprintf(out, "  %llx: ", cs + k);
        for (SIZE_T j = 0; j < 16 && k + j < got; j++) fprintf(out, "%02x ", ctx[k + j]);
        fprintf(out, " |");
        for (SIZE_T j = 0; j < 16 && k + j < got; j++) {
            unsigned char c = ctx[k + j];
            fputc((c >= 32 && c < 127) ? c : '.', out);
        }
        fprintf(out, "|\n");
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: memscan.exe <exename> <outfile> <pattern> [pattern...]\n");
        return 1;
    }
    const char *exe = argv[1];
    out = fopen(argv[2], "w");
    if (!out) { fprintf(stderr, "cannot open outfile\n"); return 1; }
    int npat = argc - 3;
    char **pats = &argv[3];

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, exe)) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { fprintf(out, "open failed pid=%lu err=%lu\n", pid, GetLastError()); fclose(out); return 1; }
    fprintf(out, "pid=0x%lx patterns=%d\n", pid, npat);

    unsigned char *buf = malloc(0x100000);
    ULONG64 a = 0x10000, scanned = 0;
    int hits = 0;
    MEMORY_BASIC_INFORMATION m;
    while (a < 0x8000000000ull && VirtualQueryEx(proc, (void *)a, &m, sizeof(m)) == sizeof(m)) {
        ULONG64 base = (ULONG64)m.BaseAddress, size = m.RegionSize;
        if (m.State == MEM_COMMIT && readable(m.Protect)) {
            for (ULONG64 off = 0; off < size; off += 0x100000) {
                SIZE_T want = (SIZE_T)((size - off) > 0x100000 ? 0x100000 : (size - off));
                SIZE_T got = 0;
                if (!ReadProcessMemory(proc, (void *)(base + off), buf, want, &got) || !got) continue;
                scanned += got;
                for (int p = 0; p < npat; p++) {
                    SIZE_T pl = strlen(pats[p]);
                    for (SIZE_T i = 0; i + pl <= got; i++) {
                        if (memcmp(buf + i, pats[p], pl) == 0) {
                            dump_context(proc, base + off + i);
                            hits++;
                            i += pl - 1;
                            if (hits > 400) { fprintf(out, "\n(hit cap reached)\n"); goto done; }
                        }
                    }
                }
            }
        }
        a = base + size;
    }
done:
    fprintf(out, "\nscanned=%llu MB hits=%d\nEND\n", scanned >> 20, hits);
    fclose(out);
    return 0;
}
