// smcquery: the minimal reproducer for "FEX's SMC write trap is visible through NtQueryVirtualMemory" (the FEX issue
// text uses the same steps): allocate one RWX page, run code on it once so FEX translates it, then query it.
// Expected PAGE_EXECUTE_READWRITE (0x40); with the trap visible it reads PAGE_EXECUTE_READ (0x20).
// usage: smcquery.exe   (output D:\aoe\smcquery.txt)
#include <windows.h>
#include <stdio.h>
#include <string.h>

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR cmd, int d)
{
    FILE *o = fopen("D:\\aoe\\smcquery.txt", "w");
    if (!o) return 1;
    void *p = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    MEMORY_BASIC_INFORMATION m;
    VirtualQuery(p, &m, sizeof(m));
    fprintf(o, "before running: Protect = 0x%lx RegionSize = 0x%llx\n", m.Protect, (unsigned long long)m.RegionSize);
    memcpy(p, "\xc3", 1);       // ret
    ((void (*)(void))p)();      // run it once so FEX translates the page
    VirtualQuery(p, &m, sizeof(m));
    fprintf(o, "after running:  Protect = 0x%lx RegionSize = 0x%llx\n", m.Protect, (unsigned long long)m.RegionSize);
    fclose(o);
    return 0;
}
