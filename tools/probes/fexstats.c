// fexstats: read the SMC-trap filter counters (patch 0004) out of a running process's libarm64ecfex.dll.
// usage: fexstats.exe <exename> <rva_hex> <outfile>
//   rva_hex: RVA of SmcHideStats in that exact DLL build (llvm-nm Bin/libarm64ecfex.dll | grep SmcHideStats,
//            minus the image base 0x180000000).
// The block starts with the marker "SMCHIDE1"; a missing marker means the build has no counters, or the RVA is wrong.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: fexstats.exe <exename> <rva_hex> <outfile>\n"); return 1; }
    FILE *out = fopen(argv[3], "w");
    if (!out) return 1;
    ULONG64 rva = _strtoui64(argv[2], NULL, 16);

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { fprintf(out, "open %s failed pid=%lu err=%lu\n", argv[1], pid, GetLastError()); fclose(out); return 1; }

    HMODULE mods[1024];
    DWORD need = 0;
    EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
    ULONG64 base = 0;
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++) {
        char name[MAX_PATH];
        if (GetModuleBaseNameA(proc, mods[i], name, sizeof(name)) && !_stricmp(name, "libarm64ecfex.dll"))
            base = (ULONG64)mods[i];
    }
    if (!base) { fprintf(out, "libarm64ecfex.dll not loaded in pid %lu\n", pid); fclose(out); return 1; }

    ULONG64 v[5] = { 0 };
    SIZE_T got = 0;
    BOOL ok = ReadProcessMemory(proc, (void *)(base + rva), v, sizeof(v), &got);
    char marker[9] = { 0 };
    memcpy(marker, &v[0], 8);
    fprintf(out, "pid=%lu libarm64ecfex.dll base=%llx read=%d marker=%s\n", pid, base, ok, marker);
    fprintf(out, "queries_filtered=%llu queries_corrected=%llu protects_filtered=%llu protects_corrected=%llu\n",
            v[1], v[2], v[3], v[4]);
    fclose(out);
    CloseHandle(proc);
    return 0;
}
