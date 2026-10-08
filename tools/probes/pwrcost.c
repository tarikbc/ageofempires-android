// pwrcost: time CallNtPowerInformation(ProcessorInformation) the way AoE IV calls it every frame (exe+3aeaa10:
// level 11, no input, a buffer of one PROCESSOR_POWER_INFORMATION per logical CPU), and print the first entry.
// usage: pwrcost.exe [calls]   (output D:\aoe\pwrcost.txt)
#include <windows.h>
#include <powrprof.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState; } PPI;

// A crash inside the call is written to D:\aoe\pwrcost.txt (code, address, registers) instead of vanishing.
static LONG CALLBACK on_exception(EXCEPTION_POINTERS *e)
{
    FILE *o = fopen("D:\\aoe\\pwrcost.txt", "w");
    if (o) {
        CONTEXT *c = e->ContextRecord;
        fprintf(o, "exception 0x%lx at %p\nrip %llx rsp %llx rax %llx rcx %llx rdx %llx r8 %llx r9 %llx\n",
                e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, c->Rip, c->Rsp, c->Rax,
                c->Rcx, c->Rdx, c->R8, c->R9);
        for (DWORD i = 0; i < e->ExceptionRecord->NumberParameters; i++)
            fprintf(o, "param %lu %llx\n", i, (unsigned long long)e->ExceptionRecord->ExceptionInformation[i]);
        fclose(o);
    }
    ExitProcess(3);
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR cmd, int d)
{
    AddVectoredExceptionHandler(1, on_exception);
    int calls = atoi(cmd) > 0 ? atoi(cmd) : 200;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    PPI *buf = calloc(si.dwNumberOfProcessors, sizeof(PPI));
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    LONG st = CallNtPowerInformation(ProcessorInformation, NULL, 0, buf, si.dwNumberOfProcessors * sizeof(PPI));
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < calls; i++)
        CallNtPowerInformation(ProcessorInformation, NULL, 0, buf, si.dwNumberOfProcessors * sizeof(PPI));
    QueryPerformanceCounter(&t1);
    FILE *o = fopen("D:\\aoe\\pwrcost.txt", "w");
    if (!o) return 1;
    double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart / calls;
    fprintf(o, "status 0x%lx, %lu cpus, %d calls: %.1f us per call\n", st, si.dwNumberOfProcessors, calls, us);
    for (DWORD i = 0; i < si.dwNumberOfProcessors; i++)
        fprintf(o, "cpu %lu max %lu cur %lu limit %lu\n", buf[i].Number, buf[i].MaxMhz, buf[i].CurrentMhz, buf[i].MhzLimit);
    fclose(o);
    return 0;
}
