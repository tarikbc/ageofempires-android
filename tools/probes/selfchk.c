// selfchk: confirm that the waitq_fix ntdll patch is loaded, then drive the RtlWaitOnAddress /
// RtlWakeAddress paths through critical sections, SRW locks and WaitOnAddress.
// usage: selfchk.exe   (output D:\selfchk.txt)
#include <windows.h>
#include <stdio.h>
int main(void)
{
    FILE *f = fopen("D:\\selfchk.txt", "w");
    unsigned char *nt = (unsigned char *)GetModuleHandleA("ntdll.dll");
    fprintf(f, "ntdll=%p lock_site=%08lx cave=%08lx %08lx\n", nt, *(DWORD *)(nt + 0xce524), *(DWORD *)(nt + 0xb491c), *(DWORD *)(nt + 0xb4920));
    // exercise WaitOnAddress/WakeByAddress paths through critical sections and SRW locks
    CRITICAL_SECTION cs; InitializeCriticalSection(&cs);
    SRWLOCK srw = SRWLOCK_INIT;
    for (int i = 0; i < 100000; i++) { EnterCriticalSection(&cs); LeaveCriticalSection(&cs); AcquireSRWLockExclusive(&srw); ReleaseSRWLockExclusive(&srw); }
    LONG v = 0, cmp = 1;
    WakeByAddressAll(&v); WakeByAddressSingle(&v);
    BOOL r = WaitOnAddress(&v, &cmp, sizeof(v), 10);
    fprintf(f, "waitonaddress ret=%d err=%lu\nEND\n", r, r ? 0 : GetLastError());
    fclose(f);
    return 0;
}
