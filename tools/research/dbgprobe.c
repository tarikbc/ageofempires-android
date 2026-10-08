// dbgprobe: report every standard Windows anti-debug / anti-VM signal as seen from inside the
// Wine session, so we can tell whether the ARM64EC+FEX environment falsely looks "debugged".
// usage: dbgprobe.exe   (output D:\dbgprobe.txt)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <intrin.h>
#include <psapi.h>

typedef NTSTATUS (WINAPI *pNtQIP)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (WINAPI *pNtQIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static FILE *f;
#define LOG(...) do { fprintf(f, __VA_ARGS__); fflush(f); } while (0)

int main(void)
{
    f = fopen("D:\\dbgprobe.txt", "w");
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    pNtQIP NtQIP = (pNtQIP)GetProcAddress(nt, "NtQueryInformationProcess");
    pNtQIT NtQIT = (pNtQIT)GetProcAddress(nt, "NtQueryInformationThread");

    LOG("=== basic ===\n");
    LOG("IsDebuggerPresent=%d\n", IsDebuggerPresent());
    BOOL remote = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote);
    LOG("CheckRemoteDebuggerPresent=%d\n", remote);

    unsigned char *peb = (unsigned char *)__readgsqword(0x60);
    /* NtGlobalFlag lives at PEB+0xBC on x64; winternl.h in mingw does not expose it */
    LOG("PEB=%p BeingDebugged=%d NtGlobalFlag=0x%lx (FLG_HEAP_ENABLE_TAIL_CHECK 0x10 etc.)\n",
        peb, peb ? peb[2] : -1, peb ? *(unsigned long *)(peb + 0xBC) : 0);

    LOG("\n=== NtQueryInformationProcess ===\n");
    if (NtQIP) {
        ULONG_PTR v = 0; ULONG ret = 0; NTSTATUS st;
        st = NtQIP(GetCurrentProcess(), 7 /*ProcessDebugPort*/, &v, sizeof(v), &ret);
        LOG("  ProcessDebugPort          st=0x%08lx value=%llx\n", (unsigned long)st, (unsigned long long)v);
        v = 0;
        st = NtQIP(GetCurrentProcess(), 30 /*ProcessDebugObjectHandle*/, &v, sizeof(v), &ret);
        LOG("  ProcessDebugObjectHandle  st=0x%08lx value=%llx\n", (unsigned long)st, (unsigned long long)v);
        v = 0xffffffff;
        st = NtQIP(GetCurrentProcess(), 31 /*ProcessDebugFlags*/, &v, sizeof(v), &ret);
        LOG("  ProcessDebugFlags         st=0x%08lx value=%llx (1 = no debugger)\n", (unsigned long)st, (unsigned long long)v);
        v = 0;
        st = NtQIP(GetCurrentProcess(), 0x24 /*ProcessBasicInformation*/, &v, sizeof(v), &ret);
        LOG("  ProcessBasicInformation   st=0x%08lx\n", (unsigned long)st);
    } else LOG("  NtQIP unavailable\n");

    LOG("\n=== NtQueryInformationThread(ThreadHideFromDebugger) ===\n");
    if (NtQIT) {
        ULONG v = 0; ULONG ret = 0;
        NTSTATUS st = NtQIT(GetCurrentThread(), 0x24 /*ThreadHideFromDebugger*/, &v, sizeof(v), &ret);
        LOG("  ThreadHideFromDebugger st=0x%08lx value=%lu (probe only)\n", (unsigned long)st, v);
    }

    LOG("\n=== TEB ===\n");
    LOG("  TEB=%p\n", (void *)__readgsqword(0x30));

    LOG("\n=== CPUID (what the game's own checks would see) ===\n");
    int regs[4];
    __cpuid(regs, 0);
    char vendor[13] = {0};
    memcpy(vendor, &regs[1], 4); memcpy(vendor + 4, &regs[3], 4); memcpy(vendor + 8, &regs[2], 4);
    LOG("  vendor='%s' maxleaf=0x%x\n", vendor, regs[0]);
    __cpuid(regs, 1);
    LOG("  leaf1 eax=0x%08x (family %d model %d stepping %d)\n", regs[0],
        ((regs[0] >> 8) & 0xf) + ((regs[0] >> 20) & 0xff), ((regs[0] >> 4) & 0xf) + (((regs[0] >> 16) & 0xf) << 4),
        regs[0] & 0xf);
    LOG("  leaf1 ecx=0x%08x edx=0x%08x  hypervisor_bit=%d\n", regs[2], regs[3], (regs[2] >> 31) & 1);
    __cpuid(regs, 0x40000000);
    {
        char hv[13] = {0};
        memcpy(hv, &regs[1], 4); memcpy(hv + 4, &regs[2], 4); memcpy(hv + 8, &regs[3], 4);
        LOG("  hypervisor leaf 0x40000000 eax=0x%x vendor='%s'\n", regs[0], hv);
    }
    __cpuid(regs, 7);
    LOG("  leaf7 ebx=0x%08x\n", regs[1]);

    LOG("\n=== modules (loaded in this probe process) ===\n");
    HMODULE mods[256]; DWORD need = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) {
        char nm[MAX_PATH];
        for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 40; i++) {
            if (GetModuleFileNameA(mods[i], nm, sizeof(nm)))
                LOG("  %s\n", nm);
        }
    }
    LOG("\n=== environment ===\n");
    LOG("  WINEDEBUG=%s\n", getenv("WINEDEBUG") ? getenv("WINEDEBUG") : "(unset)");
    LOG("  PROCESSOR_ARCHITECTURE=%s\n", getenv("PROCESSOR_ARCHITECTURE") ? getenv("PROCESSOR_ARCHITECTURE") : "(unset)");
    LOG("END\n");
    fclose(f);
    return 0;
}
