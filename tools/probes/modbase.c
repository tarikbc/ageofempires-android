// modbase: print the base address and size of one module in a process (e.g. libarm64ecfex.dll in the game), so a
// probe such as peek can read a FEX global at base + RVA.
// usage: modbase.exe <exename> <module> <outfile>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 4) return 1;
    FILE *out = fopen(argv[3], "w");
    if (!out) return 1;
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
    CloseHandle(snap);
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { fprintf(out, "pid=%lu open failed\nEND\n", pid); fclose(out); return 1; }
    HMODULE mods[1024];
    DWORD need = 0;
    EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_ALL);
    for (DWORD i = 0; i < need / sizeof(HMODULE); i++) {
        char name[MAX_PATH];
        MODULEINFO mi;
        if (GetModuleBaseNameA(proc, mods[i], name, sizeof(name)) && !_stricmp(name, argv[2]) &&
            GetModuleInformation(proc, mods[i], &mi, sizeof(mi)))
            fprintf(out, "pid=%lu base=%llx size=%lx\n", pid, (unsigned long long)(ULONG_PTR)mi.lpBaseOfDll, mi.SizeOfImage);
    }
    fprintf(out, "END\n");
    fclose(out);
    return 0;
}
