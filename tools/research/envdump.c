// envdump: print the full environment of a process in the Wine session.
#include <windows.h>
#include <stdio.h>
int main(void)
{
    FILE *out = fopen("D:\\aoe\\envdump.txt", "w");
    if (!out) return 1;
    fprintf(out, "=== environment of this session process ===\n\n");
    LPCH env = GetEnvironmentStringsA();
    if (env) {
        for (LPCH p = env; *p; p += strlen(p) + 1) {
            if (strlen(p) > 0) fprintf(out, "  %s\n", p);
        }
        FreeEnvironmentStringsA(env);
    }
    fprintf(out, "\n=== Wow64 / emulator registry ===\n");
    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Wow64\\amd64", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        char buf[512] = {0}; DWORD sz = sizeof(buf), type = 0;
        if (RegQueryValueExA(k, NULL, NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            fprintf(out, "  HKLM\\Software\\Microsoft\\Wow64\\amd64 = %s\n", buf);
        RegCloseKey(k);
    } else fprintf(out, "  key not found\n");
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
