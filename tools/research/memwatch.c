// memwatch: hash the game image in blocks over time and report any block that CHANGES.
// Idea: if Aegis kills on a periodic integrity check, whatever it hashes is being
// modified by the environment (ARM64EC/FEX/Wine) -- and the change will show up here.
//
// usage: memwatch.exe <exe-name> <outfile> <seconds> [interval_ms]
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK (64 * 1024)
#define MAXBLOCKS 4096

static unsigned long long fnv(const unsigned char *p, size_t n)
{
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static DWORD find_pid(const char *name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32First(snap, &pe)) do {
        if (!_stricmp(pe.szExeFile, name)) { pid = pe.th32ProcessID; break; }
    } while (Process32Next(snap, &pe));
    CloseHandle(snap);
    return pid;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: memwatch.exe <exe> <out> <seconds> [interval_ms]\n"); return 1; }
    FILE *out = fopen(argv[2], "w");
    if (!out) return 1;
    int seconds = atoi(argv[3]);
    int interval = argc > 4 ? atoi(argv[4]) : 15000;

    unsigned long long *prev = calloc(MAXBLOCKS, sizeof(*prev));
    unsigned char *buf = malloc(BLOCK);
    int *seen = calloc(MAXBLOCKS, sizeof(int));

    DWORD t0 = GetTickCount();
    while ((int)((GetTickCount() - t0) / 1000) < seconds) {
        DWORD pid = find_pid(argv[1]);
        if (!pid) { fprintf(out, "[%4lus] process not found\n", (GetTickCount()-t0)/1000); fflush(out); break; }
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!h) h = OpenProcess(PROCESS_VM_READ, FALSE, pid);
        if (!h) { fprintf(out, "[%4lus] OpenProcess err=%lu\n", (GetTickCount()-t0)/1000, GetLastError()); fflush(out); Sleep(interval); continue; }

        // read SizeOfImage straight out of the target's PE headers (image base is fixed: no ASLR)
        unsigned char hdr[0x1000]; SIZE_T got = 0;
        ULONGLONG base = 0x140000000ULL;
        DWORD image_size = 0;
        if (ReadProcessMemory(h, (LPCVOID)base, hdr, sizeof(hdr), &got) && got >= 0x100) {
            DWORD e_lfanew = *(DWORD *)(hdr + 0x3C);
            if (e_lfanew + 0x60 < got) {
                WORD magic = *(WORD *)(hdr + e_lfanew + 0x18);
                DWORD off = (magic == 0x20B) ? 0x38 : 0x38;   // SizeOfImage offset in both optional headers
                image_size = *(DWORD *)(hdr + e_lfanew + 0x18 + off);
            }
        }
        if (!image_size || image_size > MAXBLOCKS * BLOCK) image_size = 160 * 1024 * 1024;

        DWORD el = (GetTickCount() - t0) / 1000;
        int changed = 0;
        for (DWORD off = 0; off < image_size; off += BLOCK) {
            SIZE_T n = 0;
            if (!ReadProcessMemory(h, (LPCVOID)(base + off), buf, BLOCK, &n) || !n) continue;
            int idx = off / BLOCK;
            unsigned long long hh = fnv(buf, n);
            if (seen[idx] && prev[idx] != hh) {
                changed++;
                // find the first differing byte vs. what we expect is unknowable now; just report the block
                fprintf(out, "  [%4lus] CHANGED block @0x%llx (rva 0x%lx) size=%zu\n",
                        el, base + off, off, n);
                fflush(out);
            }
            prev[idx] = hh; seen[idx] = 1;
        }
        fprintf(out, "[%4lus] scanned %lu bytes, %d changed blocks%s\n",
                el, image_size, changed, changed ? "" : "  (stable)");
        fflush(out);
        CloseHandle(h);
        Sleep(interval);
    }
    fprintf(out, "END\n");
    fclose(out);
    return 0;
}
