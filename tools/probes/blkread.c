// blkread: copy the decoded-block dump (job-local FEX experiment) out of a running process.
// usage: blkread.exe <exename> <rva_hex of BlockDumpInfo> <outfile>
// Writes the 48-byte header followed by the used part of the buffer.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 4) return 1;
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
    if (!proc) { fprintf(log, "open failed pid=%lu err=%lu\n", pid, GetLastError()); fclose(log); return 1; }
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
    ULONG64 hdr[6] = { 0 };
    SIZE_T got = 0;
    if (!ReadProcessMemory(proc, (void *)(base + rva), hdr, sizeof(hdr), &got) || memcmp(&hdr[0], "BLKDUMP1", 8)) {
        fprintf(log, "header read failed or marker missing\n");
        fclose(log);
        return 1;
    }
    unsigned char *buf = malloc((size_t)hdr[3] + 1);
    if (!buf || (hdr[3] && !ReadProcessMemory(proc, (void *)hdr[1], buf, (SIZE_T)hdr[3], &got))) {
        fprintf(log, "buffer read failed err=%lu\n", GetLastError());
        fclose(log);
        return 1;
    }
    FILE *out = fopen(argv[3], "wb");
    fwrite(hdr, 8, 6, out);
    fwrite(buf, 1, (size_t)hdr[3], out);
    fclose(out);
    fprintf(log, "pid=%lu fex=%llx used=%llu records=%llu dropped=%llu\n", pid, base, hdr[3], hdr[5], hdr[4]);
    fclose(log);
    return 0;
}
