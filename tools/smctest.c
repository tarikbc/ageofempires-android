// smctest: allocate RWX memory, then ask the OS what protection it has.
// Under FEX with SMCChecks=mtrack, FEX arms a write trap on guest RWX pages by
// setting PAGE_EXECUTE_READ. If that is visible here, FEX is leaking its SMC
// instrumentation to the guest -- which is what Aegis would be detecting.
#include <windows.h>
#include <stdio.h>
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
static void report(FILE *f, const char *label, void *p)
{
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(p, &m, sizeof(m)))
        fprintf(f, "%-34s Protect=%-8s (0x%lx)%s\n", label, pn(m.Protect), m.Protect,
                (m.Protect & 0xff) == PAGE_EXECUTE_READ && 1 ? "   <-- write permission missing" : "");
    else
        fprintf(f, "%-34s VirtualQuery failed err=%lu\n", label, GetLastError());
}
int main(void)
{
    FILE *f = fopen("D:\\aoe\\smctest.txt", "w");
    if (!f) return 1;
    fprintf(f, "=== FEX SMC trap visibility test ===\n\n");

    void *p = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    fprintf(f, "VirtualAlloc(PAGE_EXECUTE_READWRITE) -> %p (err=%lu)\n\n", p, GetLastError());
    if (!p) { fclose(f); return 0; }

    report(f, "immediately after VirtualAlloc", p);
    Sleep(100);
    report(f, "after 100ms", p);

    // Write code (16 bytes of x86-64: mov eax, 42; ret + padding)
    unsigned char code[] = {0xB8,0x2A,0x00,0x00,0x00,0xC3};
    memcpy(p, code, sizeof(code));
    fprintf(f, "\nwrote %d bytes of code into the page\n", (int)sizeof(code));
    report(f, "after writing to the page", p);

    // Try to execute it
    typedef int (*fn)(void);
    fn call = (fn)p;
    int r = call();   /* valid code: mov eax,42 ; ret */
    fprintf(f, "executed the page -> returned %d\n", r);
    report(f, "after executing the page", p);

    // Set it RWX explicitly again via VirtualProtect and re-query
    DWORD old = 0;
    if (VirtualProtect(p, 0x1000, PAGE_EXECUTE_READWRITE, &old)) {
        fprintf(f, "\nVirtualProtect(..., PAGE_EXECUTE_READWRITE) ok, previous=0x%lx\n", old);
        report(f, "after explicit VirtualProtect RWX", p);
    }

    fprintf(f, "\ninterpretation: RWX means FEX's trap is NOT visible to the guest.\n"
               "                RX  means FEX stripped WRITE from a page the guest set RWX.\n");
    fprintf(f, "\nEND\n");
    fclose(f);
    return 0;
}
