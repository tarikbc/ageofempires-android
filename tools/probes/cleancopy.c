// cleancopy: compare a loaded system DLL with a fresh image mapping of its file, the way an anti-tamper "clean copy"
// check would: resolve a few exports in both (GetProcAddress vs. the mapped copy's export table) and compare the RVA
// and the first 16 bytes at that RVA.
// usage: cleancopy.exe [outfile]    default D:\aoe\cleancopy.txt
// Build: x86_64-w64-mingw32-gcc -O1 -static -o cleancopy.exe cleancopy.c -lntdll
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

typedef NTSTATUS (NTAPI *NtCreateSection_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
typedef NTSTATUS (NTAPI *NtMapViewOfSection_t)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG);

static DWORD export_rva(BYTE *base, const char *name)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!d.VirtualAddress) return 0;
    IMAGE_EXPORT_DIRECTORY *e = (IMAGE_EXPORT_DIRECTORY *)(base + d.VirtualAddress);
    DWORD *names = (DWORD *)(base + e->AddressOfNames), *funcs = (DWORD *)(base + e->AddressOfFunctions);
    WORD *ords = (WORD *)(base + e->AddressOfNameOrdinals);
    for (DWORD i = 0; i < e->NumberOfNames; i++)
        if (!strcmp((char *)base + names[i], name)) return funcs[ords[i]];
    return 0;
}

static void hex(FILE *f, const BYTE *p) { for (int i = 0; i < 16; i++) fprintf(f, "%02x ", p[i]); }

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "D:\\aoe\\cleancopy.txt", "w");
    if (!f) return 1;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    NtCreateSection_t pNtCreateSection = (NtCreateSection_t)GetProcAddress(ntdll, "NtCreateSection");
    NtMapViewOfSection_t pNtMapViewOfSection = (NtMapViewOfSection_t)GetProcAddress(ntdll, "NtMapViewOfSection");
    const char *dlls[] = {"kernelbase.dll", "kernel32.dll", "ntdll.dll"};
    const char *funcs[] = {"GetWsChangesEx", "K32GetWsChangesEx", "VirtualProtect", "GetProcAddress", "IsDebuggerPresent",
                           "NtQueryInformationProcess", "LoadLibraryExW", "CreateFileW"};
    for (int d = 0; d < 3; d++) {
        HMODULE h = LoadLibraryA(dlls[d]);
        char path[MAX_PATH];
        GetModuleFileNameA(h, path, sizeof(path));
        fprintf(f, "== %s loaded at %p from %s\n", dlls[d], (void *)h, path);
        HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE sec = NULL;
        PVOID view = NULL;
        SIZE_T vsize = 0;
        NTSTATUS st = file == INVALID_HANDLE_VALUE ? -1 : pNtCreateSection(&sec, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, NULL, NULL, PAGE_READONLY, SEC_IMAGE, file);
        if (!st) st = pNtMapViewOfSection(sec, GetCurrentProcess(), &view, 0, 0, NULL, &vsize, 1 /* ViewShare */, 0, PAGE_READONLY);
        fprintf(f, "   image mapping of the file: status %08lx at %p\n", (unsigned long)st, view);
        for (int i = 0; i < (int)(sizeof(funcs) / sizeof(funcs[0])); i++) {
            BYTE *a = (BYTE *)GetProcAddress(h, funcs[i]);
            if (!a) continue;
            DWORD rva_loaded = (DWORD)(a - (BYTE *)h);
            DWORD rva_table = export_rva((BYTE *)h, funcs[i]);
            DWORD rva_clean = view ? export_rva((BYTE *)view, funcs[i]) : 0;
            fprintf(f, "   %-26s GetProcAddress rva %#8lx | loaded export table %#8lx | clean export table %#8lx\n",
                    funcs[i], (unsigned long)rva_loaded, (unsigned long)rva_table, (unsigned long)rva_clean);
            fprintf(f, "      loaded bytes: "); hex(f, a); fprintf(f, "\n");
            if (view && rva_loaded < vsize) {
                fprintf(f, "      clean  bytes: "); hex(f, (BYTE *)view + rva_loaded);
                fprintf(f, "%s\n", memcmp(a, (BYTE *)view + rva_loaded, 16) ? "  <-- DIFFER" : "");
            }
        }
    }
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}
