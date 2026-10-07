// exccost: time one illegal-instruction exception handled by a vectored handler (the way the game's protection
// decrypts code on demand), and, for comparison, an empty call and GetTickCount64.
// usage: exccost.exe [count] [outfile]    defaults: 5000 D:\aoe\exccost.txt
// Build: x86_64-w64-mingw32-gcc -O1 -static -o exccost.exe exccost.c
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static volatile LONG handled;

static LONG CALLBACK handler(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != STATUS_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Rip += 2; // skip ud2
    handled++;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static __attribute__((noinline)) void raise_once(void) { __asm__ volatile("ud2"); }
static __attribute__((noinline)) int empty(int x) { __asm__ volatile("" ::: "memory"); return x + 1; }

static double now_us(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 5000;
    FILE *f = fopen(argc > 2 ? argv[2] : "D:\\aoe\\exccost.txt", "w");
    if (!f) return 1;
    AddVectoredExceptionHandler(1, handler);
    for (int i = 0; i < 50; i++) raise_once(); // warm up (JIT, first-chance paths)
    double t0 = now_us();
    for (int i = 0; i < n; i++) raise_once();
    double t1 = now_us();
    volatile int s = 0;
    for (int i = 0; i < n * 100; i++) s = empty(s);
    double t2 = now_us();
    volatile ULONGLONG g = 0;
    for (int i = 0; i < n * 100; i++) g += GetTickCount64();
    double t3 = now_us();
    fprintf(f, "exceptions: %d handled=%ld, %.1f us each\n", n, (long)handled, (t1 - t0) / n);
    fprintf(f, "empty call: %.3f us each\n", (t2 - t1) / (n * 100.0));
    fprintf(f, "GetTickCount64: %.3f us each\n", (t3 - t2) / (n * 100.0));
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}
