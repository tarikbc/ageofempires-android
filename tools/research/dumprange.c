// dumprange: dump a virtual address range from a process's memory to a file.
// usage: dumprange.exe <exename> <base_hex> <size_hex> <outfile>
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: dumprange.exe <exename> <base_hex> <size_hex> <outfile>\n");
        return 1;
    }
    const char *exe = argv[1];
    ULONG64 base = _strtoui64(argv[2], NULL, 16);
    ULONG64 size = _strtoui64(argv[3], NULL, 16);
    FILE *out = fopen(argv[4], "wb");
    if (!out) { fprintf(stderr, "open outfile failed\n"); return 1; }

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, exe)) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { fprintf(stderr, "open failed pid=%lu err=%lu\n", pid, GetLastError()); return 1; }

    unsigned char *buf = malloc((size_t)size);
    SIZE_T got = 0;
    BOOL ok = ReadProcessMemory(proc, (void *)base, buf, (SIZE_T)size, &got);
    fwrite(buf, 1, (size_t)got, out);
    fclose(out);
    fprintf(stderr, "pid=0x%lx base=%llx read %llu of %llu ok=%d\n",
            pid, (unsigned long long)base, (unsigned long long)got, (unsigned long long)size, ok);
    return 0;
}
