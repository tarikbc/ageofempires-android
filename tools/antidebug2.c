// antidebug2: probe the anti-debug surface Aegis is likely to use, and report what
// Wine answers versus what Windows answers. A call that Wine reports as
// "not implemented" is exactly the kind of thing an anti-tamper treats as tampering.
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef NTSTATUS (NTAPI *pNtSetInformationThread)(HANDLE, ULONG, PVOID, ULONG);
typedef NTSTATUS (NTAPI *pNtQueryInformationThread)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI *pNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI *pNtQueryObject)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI *pNtSetDebugFilterState)(ULONG, ULONG, BOOLEAN);

#define ThreadHideFromDebugger 0x24
#define SystemKernelDebuggerInformation 0x23
#define ObjectTypesInformation 3

struct KdInfo { BOOLEAN KdDebuggerEnabled; BOOLEAN KdDebuggerNotPresent; };

static FILE *out;
static void st(const char *what, NTSTATUS s) { fprintf(out, "  %-46s 0x%08lx\n", what, (unsigned long)s); }

int main(void)
{
    out = fopen("D:\\aoe\\antidebug2.txt", "w");
    if (!out) return 1;
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    pNtSetInformationThread SetITH = (pNtSetInformationThread)GetProcAddress(nt, "NtSetInformationThread");
    pNtQueryInformationThread QryITH = (pNtQueryInformationThread)GetProcAddress(nt, "NtQueryInformationThread");
    pNtQuerySystemInformation QrySI = (pNtQuerySystemInformation)GetProcAddress(nt, "NtQuerySystemInformation");
    pNtQueryObject QryObj = (pNtQueryObject)GetProcAddress(nt, "NtQueryObject");
    pNtSetDebugFilterState SetDFS = (pNtSetDebugFilterState)GetProcAddress(nt, "NtSetDebugFilterState");

    fprintf(out, "=== anti-debug surface (Wine vs Windows expectations) ===\n\n");

    fprintf(out, "--- thread hiding ---\n");
    if (SetITH) {
        NTSTATUS s = SetITH(GetCurrentThread(), ThreadHideFromDebugger, NULL, 0);
        st("NtSetInformationThread(ThreadHideFromDebugger)", s);
        fprintf(out, "     (Windows: 0x00000000 STATUS_SUCCESS)\n");
        ULONG back = 0;
        s = QryITH(GetCurrentThread(), ThreadHideFromDebugger, &back, sizeof(back), NULL);
        st("NtQueryInformationThread(ThreadHideFromDebugger)", s);
        fprintf(out, "     (Windows: 0xC0000004 STATUS_INFO_LENGTH_MISMATCH or invalid class)\n");
        // did the TEB flag actually get set? TEB->HideFromDebugger is at 0x0? use the documented byte via NtCurrentTeb
        unsigned char *teb = (unsigned char *)NtCurrentTeb();
        fprintf(out, "     TEB=%p  byte@0x1fb=0x%02x\n", teb, teb[0x1fb]);
    } else fprintf(out, "  NtSetInformationThread not found\n");

    fprintf(out, "\n--- kernel debugger state ---\n");
    if (QrySI) {
        struct KdInfo kd = {0};
        NTSTATUS s = QrySI(SystemKernelDebuggerInformation, &kd, sizeof(kd), NULL);
        st("NtQuerySystemInformation(SystemKernelDebuggerInformation)", s);
        fprintf(out, "     KdDebuggerEnabled=%u KdDebuggerNotPresent=%u\n", kd.KdDebuggerEnabled, kd.KdDebuggerNotPresent);
        fprintf(out, "     (Windows: 0x0, enabled=0, notPresent=1)\n");
    }
    // the classic: KUSER_SHARED_DATA->KdDebuggerEnabled
    {
        unsigned char *sud = (unsigned char *)0x7FFE0000ULL;
        fprintf(out, "  SharedUserData@0x7ffe0000 KdDebuggerEnabled=0x%02x\n", sud[0x2D0]);
        fprintf(out, "     (Windows: 0x00)\n");
    }

    fprintf(out, "\n--- misc ---\n");
    SetLastError(0xDEADBEEF);
    OutputDebugStringA("antidebug2 probe\n");
    fprintf(out, "  OutputDebugString + GetLastError = 0x%08lx\n", GetLastError());
    fprintf(out, "     (Windows with NO debugger: nonzero; with debugger: unchanged)\n");

    if (QryObj) {
        ULONG sz = 0; NTSTATUS s = QryObj(GetCurrentProcess(), ObjectTypesInformation, NULL, 0, &sz);
        st("NtQueryObject(ObjectTypesInformation) size probe", s);
        fprintf(out, "     (Windows: 0xC0000004 with needed size, 0x0 with buffer)\n");
    }
    if (SetDFS) {
        NTSTATUS s = SetDFS(0, 0, FALSE);
        st("NtSetDebugFilterState", s);
        fprintf(out, "     (Windows: 0xC0000022 access denied, or 0x0)\n");
    }

    fprintf(out, "\n--- integrity-adjacent ---\n");
    {
        HANDLE h = CreateFileA("C:\\windows\\system32\\ntdll.dll", GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
        fprintf(out, "  open ntdll for read: %s\n", h == INVALID_HANDLE_VALUE ? "FAILED" : "ok");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    {
        char buf[64] = {0}; DWORD n = 0;
        HMODULE m = GetModuleHandleA(NULL);
        GetModuleFileNameA(m, buf, sizeof(buf));
        fprintf(out, "  own image: %s\n", buf);
    }

    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
