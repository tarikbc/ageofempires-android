// wakecost: what one sleep/wake hand-off between two threads costs, the way AoE IV's job threads do it ~9,000
// times per second in the late game. Two threads ping-pong a counter through WaitOnAddress/WakeByAddressSingle
// (Wine: NtWaitForAlertByThreadId / NtAlertThreadByThreadId) and through an SRW lock + condition variable; a
// third test times an uncontended system call (NtYieldExecution via SwitchToThread) for comparison.
// usage: wakecost.exe [round_trips]   (output D:\aoe\wakecost.txt)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static volatile LONG turn;
static int rounds;
static SRWLOCK lock = SRWLOCK_INIT;
static CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
static volatile LONG cv_turn;

static DWORD WINAPI addr_partner(LPVOID p)
{
    for (int i = 0; i < rounds; i++) {
        LONG want = 0;
        while (turn == want) WaitOnAddress(&turn, &want, sizeof(want), INFINITE);  // wait for 1
        turn = 0;
        WakeByAddressSingle((PVOID)&turn);
    }
    return 0;
}

static DWORD WINAPI cv_partner(LPVOID p)
{
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < rounds; i++) {
        while (cv_turn != 1) SleepConditionVariableSRW(&cv, &lock, INFINITE, 0);
        cv_turn = 0;
        WakeConditionVariable(&cv);
    }
    ReleaseSRWLockExclusive(&lock);
    return 0;
}

static double now_us(void)
{
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1e6 / f.QuadPart;
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR cmd, int d)
{
    rounds = atoi(cmd) > 0 ? atoi(cmd) : 20000;
    FILE *o = fopen("D:\\aoe\\wakecost.txt", "w");
    if (!o) return 1;

    // 1. WaitOnAddress / WakeByAddressSingle ping-pong
    HANDLE t = CreateThread(NULL, 0, addr_partner, NULL, 0, NULL);
    double t0 = now_us();
    for (int i = 0; i < rounds; i++) {
        turn = 1;
        WakeByAddressSingle((PVOID)&turn);
        LONG want = 1;
        while (turn == want) WaitOnAddress(&turn, &want, sizeof(want), INFINITE);  // wait for 0
    }
    double t1 = now_us();
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    fprintf(o, "WaitOnAddress ping-pong: %d round trips, %.2f us each (2 hand-offs)\n", rounds, (t1 - t0) / rounds);

    // 2. SRW lock + condition variable ping-pong
    t = CreateThread(NULL, 0, cv_partner, NULL, 0, NULL);
    t0 = now_us();
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < rounds; i++) {
        cv_turn = 1;
        WakeConditionVariable(&cv);
        while (cv_turn != 0) SleepConditionVariableSRW(&cv, &lock, INFINITE, 0);
    }
    ReleaseSRWLockExclusive(&lock);
    t1 = now_us();
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    fprintf(o, "SRW + condition variable ping-pong: %d round trips, %.2f us each\n", rounds, (t1 - t0) / rounds);

    // 3. a cheap system call alone
    t0 = now_us();
    for (int i = 0; i < rounds; i++) SwitchToThread();
    t1 = now_us();
    fprintf(o, "SwitchToThread (NtYieldExecution): %d calls, %.2f us each\n", rounds, (t1 - t0) / rounds);
    fclose(o);
    return 0;
}
