// thunkprobe: after FEX's export-thunk rewrite, report what changed. Run it as RelicCardinal.exe so the rewrite applies.
// usage: RelicCardinal.exe outfile
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void dump_exports(FILE *f, const char *dll)
{
    BYTE *b = (BYTE *)LoadLibraryA(dll);
    if (!b) return;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY *e = (IMAGE_EXPORT_DIRECTORY *)(b + d.VirtualAddress);
    DWORD *funcs = (DWORD *)(b + e->AddressOfFunctions);
    DWORD *names = (DWORD *)(b + e->AddressOfNames);
    WORD *ords = (WORD *)(b + e->AddressOfNameOrdinals);
    int n48 = 0, nff = 0;
    for (DWORD i = 0; i < e->NumberOfNames; i++) {
        DWORD rva = funcs[ords[i]];
        if (rva >= d.VirtualAddress && rva < d.VirtualAddress + d.Size) continue;
        BYTE *c = b + rva;
        if (c[0] == 0x48 && c[1] == 0xff && c[2] == 0x25) n48++;
        if (c[0] == 0xff && c[1] == 0x25) {
            nff++;
            fprintf(f, "  %s still FF25: %s rva %#lx next %02x\n", dll, (char *)b + names[i], (unsigned long)rva, c[6]);
        }
    }
    fprintf(f, "%s base %p: exports %lu, 48 FF 25 %d, FF 25 %d\n", dll, (void *)b, (unsigned long)e->NumberOfNames, n48, nff);
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "D:\\aoe\\thunkprobe.txt", "w");
    if (!f) return 1;
    const char *dlls[] = {"kernel32.dll", "kernelbase.dll", "ntdll.dll", "user32.dll", "advapi32.dll", "ws2_32.dll"};
    for (int i = 0; i < 6; i++) dump_exports(f, dlls[i]);
    BYTE *kb = (BYTE *)GetModuleHandleA("kernelbase.dll");
    BYTE *p = kb + 0xf5af0;
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(p, &mbi, sizeof(mbi));
    fprintf(f, "kernelbase+0xf5af0:");
    for (int i = 0; i < 16; i++) fprintf(f, " %02x", p[i]);
    fprintf(f, "\n  protect %#lx base %p size %#llx type %#lx\n", mbi.Protect, mbi.BaseAddress,
            (unsigned long long)mbi.RegionSize, mbi.Type);
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}
