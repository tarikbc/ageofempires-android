// smctest2: is FEX's SMC write trap hidden from the guest, AND still working?
//
// Hidden: VirtualQuery / QueryWorkingSetEx / VirtualProtect's old protection report what the guest set.
// Working: code rewritten after it has run is re-translated (a missing trap would run the stale code).
// Also checks that a page the guest itself sets to RX still reads back as RX, and that a trapped page in the
// middle of an RWX allocation does not split the region the guest sees.
// Output: D:\aoe\smctest2.txt. Build: x86_64-w64-mingw32-gcc -O1 -static -o smctest2.exe smctest2.c -lpsapi
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>

static FILE *f;
static const char *pn(ULONG p)
{
    switch (p & 0xff) {
    case PAGE_NOACCESS: return "NOACCESS";
    case PAGE_READONLY: return "R";
    case PAGE_READWRITE: return "RW";
    case PAGE_EXECUTE: return "X";
    case PAGE_EXECUTE_READ: return "RX";
    case PAGE_EXECUTE_READWRITE: return "RWX";
    case PAGE_EXECUTE_WRITECOPY: return "XWC";
    default: return "?";
    }
}
static void q(const char *label, void *p)
{
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(p, &m, sizeof(m)))
        fprintf(f, "  %-40s Protect=%-4s (0x%02lx) RegionSize=0x%llx\n", label, pn(m.Protect), m.Protect,
                (unsigned long long)m.RegionSize);
    else
        fprintf(f, "  %-40s VirtualQuery failed err=%lu\n", label, GetLastError());
}
static void ws(const char *label, void *p)
{
    PSAPI_WORKING_SET_EX_INFORMATION w;
    memset(&w, 0, sizeof(w));
    w.VirtualAddress = p;
    if (QueryWorkingSetEx(GetCurrentProcess(), &w, sizeof(w)))
        fprintf(f, "  %-40s Valid=%d Win32Protection=%-4s (0x%02lx)\n", label, (int)w.VirtualAttributes.Valid,
                pn((ULONG)w.VirtualAttributes.Win32Protection), (unsigned long)w.VirtualAttributes.Win32Protection);
    else
        fprintf(f, "  %-40s QueryWorkingSetEx failed err=%lu\n", label, GetLastError());
}
typedef int (*fn)(void);
static void put(void *p, int value)
{
    unsigned char code[] = {0xB8, 0, 0, 0, 0, 0xC3}; // mov eax, value ; ret
    memcpy(code + 1, &value, 4);
    memcpy(p, code, sizeof(code));
}

int main(void)
{
    f = fopen("D:\\aoe\\smctest2.txt", "w");
    if (!f) return 1;
    fprintf(f, "=== smctest2: SMC trap hidden and still working? ===\n\n");

    unsigned char *base = VirtualAlloc(NULL, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!base) { fprintf(f, "VirtualAlloc failed %lu\n", GetLastError()); fclose(f); return 0; }
    unsigned char *p = base + 0x1000; // middle page of three
    fprintf(f, "3 RWX pages at %p, code in the middle page %p\n\n", base, p);

    fprintf(f, "[1] run code once\n");
    put(p, 42);
    int r1 = ((fn)p)();
    fprintf(f, "  returned %d (expect 42)\n", r1);
    q("middle page after running", p);
    q("first page (region should cover 3 pages)", base);
    ws("working set, middle page", p);

    fprintf(f, "\n[2] rewrite the code and run it again\n");
    put(p, 43);
    q("middle page after the rewrite", p);
    int r2 = ((fn)p)();
    fprintf(f, "  returned %d (expect 43; 42 means the rewrite was missed)\n", r2);
    q("middle page after running again", p);

    fprintf(f, "\n[3] VirtualProtect RWX -> old protection\n");
    DWORD old = 0;
    VirtualProtect(p, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    fprintf(f, "  previous=%s (0x%02lx) (expect RWX)\n", pn(old), old);

    fprintf(f, "\n[4] guest sets the page RX itself\n");
    ((fn)p)(); // make sure it is trapped again first
    VirtualProtect(p, 0x1000, PAGE_EXECUTE_READ, &old);
    fprintf(f, "  VirtualProtect(RX) previous=%s (0x%02lx) (expect RWX)\n", pn(old), old);
    q("middle page after guest set RX", p);
    ((fn)p)();
    q("after running it (expect RX, not RWX)", p);
    VirtualProtect(p, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    fprintf(f, "  VirtualProtect(RWX) previous=%s (0x%02lx) (expect RX)\n", pn(old), old);

    fprintf(f, "\n[5] rewrite after the guest's own RX/RWX round trip\n");
    put(p, 44);
    int r3 = ((fn)p)();
    fprintf(f, "  returned %d (expect 44)\n", r3);
    q("middle page after running", p);

    fprintf(f, "\nEND\n");
    fclose(f);
    return 0;
}
