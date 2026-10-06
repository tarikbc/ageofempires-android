// modlist: dump every module loaded in the game process, with its path.
// Aegis carries a 386-item blocklist; anything here that Windows would never have
// loaded (the ARM64EC emulator, Wine's shims) is a candidate trigger.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>

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
    const char *target = argc > 1 ? argv[1] : "RelicCardinal.exe";
    FILE *out = fopen("D:\\aoe\\modlist.txt", "w");
    if (!out) return 1;
    DWORD pid = find_pid(target);
    fprintf(out, "=== modules of %s (pid %lu) ===\n\n", target, pid);
    if (!pid) { fprintf(out, "process not found\nEND\n"); fclose(out); return 0; }

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) { fprintf(out, "OpenProcess err=%lu\nEND\n", GetLastError()); fclose(out); return 0; }

    HMODULE mods[1024]; DWORD needed = 0;
    if (!EnumProcessModules(h, mods, sizeof(mods), &needed)) {
        fprintf(out, "EnumProcessModules err=%lu\nEND\n", GetLastError());
        CloseHandle(h); fclose(out); return 0;
    }
    int n = needed / sizeof(HMODULE);
    fprintf(out, "%d modules\n\n", n);
    for (int i = 0; i < n; i++) {
        char path[MAX_PATH] = {0};
        GetModuleFileNameExA(h, mods[i], path, sizeof(path));
        MODULEINFO mi = {0};
        GetModuleInformation(h, mods[i], &mi, sizeof(mi));
        fprintf(out, "  %-58s base=%p size=0x%lx\n", path, mi.lpBaseOfDll, (unsigned long)mi.SizeOfImage);
    }
    fprintf(out, "\nEND\n");
    CloseHandle(h);
    fclose(out);
    return 0;
}
