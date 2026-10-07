// peek: hex-dump memory of a running process found by exe name. usage: peek.exe exename addr_hex size_hex outfile
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 5) return 1;
    FILE *f = fopen(argv[4], "w");
    if (!f) return 1;
    unsigned long long addr = strtoull(argv[2], NULL, 16);
    SIZE_T size = (SIZE_T)strtoull(argv[3], NULL, 16);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = {sizeof(pe)};
    DWORD pid = 0;
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
    fprintf(f, "pid %lu\n", pid);
    HANDLE h = pid ? OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid) : NULL;
    unsigned char *buf = malloc(size);
    SIZE_T got = 0;
    if (h && ReadProcessMemory(h, (void *)addr, buf, size, &got)) {
        for (SIZE_T i = 0; i < got; i += 16) {
            fprintf(f, "%llx:", addr + i);
            for (SIZE_T j = i; j < i + 16 && j < got; j++) fprintf(f, " %02x", buf[j]);
            fprintf(f, "\n");
        }
    } else {
        fprintf(f, "read failed %lu\n", GetLastError());
    }
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}
