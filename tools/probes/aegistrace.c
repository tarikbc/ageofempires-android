// aegistrace: copy the syscall trace (FEX patch 0005) out of a running process.
// usage: aegistrace.exe <exename> <rva_hex of AegisTrace> <outfile>
//   rva: llvm-nm Bin/libarm64ecfex.dll | grep ' AegisTrace$', minus the image base 0x180000000.
// Writes the 136-byte header followed by the whole ring buffer (Capacity * 64 bytes) to <outfile>;
// tools/research/parse_aegistrace.py decodes it.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: aegistrace.exe <exename> <rva_hex> <outfile>\n"); return 1; }
    char logname[MAX_PATH];
    snprintf(logname, sizeof(logname), "%s.log", argv[3]);
    FILE *log = fopen(logname, "w");
    if (!log) return 1;
    ULONG64 rva = _strtoui64(argv[2], NULL, 16);

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { fprintf(log, "open %s failed pid=%lu err=%lu\n", argv[1], pid, GetLastError()); fclose(log); return 1; }

    HMODULE mods[1024];
    DWORD need = 0;
    EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
    ULONG64 base = 0;
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++) {
        char name[MAX_PATH];
        if (GetModuleBaseNameA(proc, mods[i], name, sizeof(name)) && !_stricmp(name, "libarm64ecfex.dll"))
            base = (ULONG64)mods[i];
    }
    if (!base) { fprintf(log, "libarm64ecfex.dll not loaded\n"); fclose(log); return 1; }

    ULONG64 hdr[19] = { 0 }; // Marker, Buffer, Capacity, WriteIndex, ExeBase, ExeEnd, RegionLo, RegionHi, TicksPerSecond, WatchMask[8], pad
    SIZE_T got = 0;
    if (!ReadProcessMemory(proc, (void *)(base + rva), hdr, 17 * 8, &got) || memcmp(&hdr[0], "AEGTRAC1", 8)) {
        fprintf(log, "header read failed or marker missing (read=%llu)\n", (unsigned long long)got);
        fclose(log);
        return 1;
    }
    ULONG64 bytes = hdr[2] * 64;
    unsigned char *buf = malloc((size_t)bytes);
    if (!buf || !hdr[1] || !ReadProcessMemory(proc, (void *)hdr[1], buf, (SIZE_T)bytes, &got)) {
        fprintf(log, "buffer read failed (buffer=%llx capacity=%llu err=%lu)\n", hdr[1], hdr[2], GetLastError());
        fclose(log);
        return 1;
    }
    FILE *out = fopen(argv[3], "wb");
    fwrite(hdr, 8, 17, out);
    fwrite(buf, 1, (size_t)got, out);
    fclose(out);
    fprintf(log, "pid=%lu fex=%llx exe=%llx..%llx region=%llx..%llx capacity=%llu written=%llu ticks/s=%llu\n", pid, base,
            hdr[4], hdr[5], hdr[6], hdr[7], hdr[2], hdr[3], hdr[8]);
    fclose(log);
    return 0;
}
