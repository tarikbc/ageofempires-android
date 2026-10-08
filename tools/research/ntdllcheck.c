// ntdllcheck: which ntdll build is actually MAPPED in this process?
// The invoke patch writes 0xE9 (jmp) at rva 0xEC050 (pristine: 0x4C).
// The waitq patch turns the ldaxr at rva 0xCDA74 into a branch (0x14......).
#include <windows.h>
#include <stdio.h>
int main(void)
{
    FILE *out = fopen("D:\\aoe\\ntdllcheck.txt", "w");
    if (!out) return 1;
    HMODULE m = GetModuleHandleA("ntdll.dll");
    unsigned char *b = (unsigned char *)m;
    fprintf(out, "=== mapped ntdll build ===\n");
    fprintf(out, "base=%p\n\n", m);

    unsigned char inv = b[0xEC050];
    fprintf(out, "rva 0xEC050 (invoke patch site): 0x%02x  -> %s\n", inv,
            inv == 0xE9 ? "PATCHED (jmp to cave)" : (inv == 0x4C ? "PRISTINE" : "?"));
    fprintf(out, "   bytes: %02x %02x %02x %02x %02x\n", b[0xEC050], b[0xEC051], b[0xEC052], b[0xEC053], b[0xEC054]);

    unsigned int w1 = *(unsigned int *)(b + 0xCDA74);
    fprintf(out, "\nrva 0xCDA74 (waitq site stub_U1): 0x%08x\n", w1);
    fprintf(out, "   -> %s\n", (w1 >> 26) == 0x05 ? "PATCHED (b <cave>)" : (w1 == 0x885FFC1Fu ? "PRISTINE (ldaxr w31,[x8])" : "?"));
    unsigned int w2 = *(unsigned int *)(b + 0xCDA00);
    fprintf(out, "rva 0xCDA00 (waitq site stub_L1): 0x%08x\n", w2);
    fprintf(out, "   -> %s\n", (w2 >> 26) == 0x05 ? "PATCHED (b <cave>)" : (w2 == 0x885FFD0Cu ? "PRISTINE (ldaxr w12,[x8])" : "?"));

    // also compare a chunk of the file on disk at the same rva
    HANDLE h = CreateFileA("C:\\windows\\system32\\ntdll.dll", GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        unsigned char fb[8] = {0}; DWORD got = 0;
        SetFilePointer(h, 0xEC050, NULL, FILE_BEGIN);
        ReadFile(h, fb, 5, &got, NULL);
        fprintf(out, "\nsystem32 file @0xEC050: %02x %02x %02x %02x %02x (%lu bytes)\n", fb[0], fb[1], fb[2], fb[3], fb[4], got);
        CloseHandle(h);
    } else fprintf(out, "\ncould not open system32 ntdll (err=%lu)\n", GetLastError());
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
