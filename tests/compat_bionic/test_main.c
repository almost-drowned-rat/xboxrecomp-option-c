/*
 * compat_bionic test (2026-11-11):win32_compat.c's Android branches on glibc.
 *   1. TerminateThread without pthread_cancel: refuses (FALSE, ERROR_NOT_SUPPORTED), the thread is NOT reported
 *      as ended (a wait still times out) and it keeps running until it ends itself
 *   2. a file mapping through the memfd system call: two views of one mapping see each other's writes
 *   3. SuspendThread suspends: a self-suspend blocks until ResumeThread, a thread suspended by another stops at
 *      its next w32_suspend_point (all POSIX hosts; 2026-11-11)
 */
#include "win32_compat.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, what) do { if (c) printf("  PASS  %s\n", what); \
                            else { printf("  FAIL  %s (line %d)\n", what, __LINE__); fails++; } } while (0)

static volatile LONG g_stop, g_ticks, g_after_self, g_pts;

static DWORD WINAPI spin(LPVOID p)
{
    (void)p;
    while (!g_stop) {
        InterlockedIncrement(&g_ticks);
        Sleep(1);
    }
    return 7;
}

static DWORD WINAPI self_suspend(LPVOID p)
{
    (void)p;
    SuspendThread(GetCurrentThread());          /* stops here until resumed */
    g_after_self = 1;
    return 0;
}

static DWORD WINAPI at_points(LPVOID p)
{
    (void)p;
    while (!g_stop) {
        InterlockedIncrement(&g_pts);
        Sleep(1);
        w32_suspend_point();                    /* where the kernel would deliver the suspend */
    }
    return 0;
}

int main(void)
{
    HANDLE t, m;
    DWORD code = 0;
    LONG before;
    unsigned char *a, *b;

    printf("TerminateThread without pthread_cancel\n");
    t = CreateThread(NULL, 0, spin, NULL, 0, NULL);
    CHECK(t != NULL, "thread started");
    Sleep(20);
    SetLastError(0);
    CHECK(!TerminateThread(t, 3) && GetLastError() == ERROR_NOT_SUPPORTED, "TerminateThread refuses: ERROR_NOT_SUPPORTED");
    CHECK(WaitForSingleObject(t, 30) == WAIT_TIMEOUT, "the thread is not reported as ended");
    before = g_ticks;
    Sleep(30);
    CHECK(g_ticks > before, "and it is still running");
    g_stop = 1;
    CHECK(WaitForSingleObject(t, 2000) == WAIT_OBJECT_0 && GetExitCodeThread(t, &code) && code == 7,
          "it ends itself with its own exit code");
    CloseHandle(t);

    printf("SuspendThread really suspends (CRI's movie threads, 2026-11-11)\n");
    g_stop = 0;
    t = CreateThread(NULL, 0, self_suspend, NULL, 0, NULL);
    Sleep(50);
    CHECK(t && !g_after_self, "a thread that suspends itself stops");
    CHECK(t && ResumeThread(t) == 1 && WaitForSingleObject(t, 2000) == WAIT_OBJECT_0 && g_after_self,
          "and goes on after ResumeThread");
    if (t)
        CloseHandle(t);
    t = CreateThread(NULL, 0, at_points, NULL, 0, NULL);
    Sleep(30);
    CHECK(t && SuspendThread(t) == 0, "another thread suspended (previous count 0)");
    Sleep(20);                                  /* it reaches its next suspension point */
    before = g_pts;
    Sleep(50);
    CHECK(g_pts == before, "it stops at its suspension point");
    CHECK(ResumeThread(t) == 1, "resumed (previous count 1)");
    Sleep(50);
    CHECK(g_pts > before, "and runs again");
    g_stop = 1;
    if (t) {
        WaitForSingleObject(t, 2000);
        CloseHandle(t);
    }

    printf("file mapping through the memfd system call\n");
    m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 1 << 16, NULL);
    CHECK(m != NULL, "mapping of 64 KB created");
    a = (unsigned char *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 1 << 16);
    b = (unsigned char *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 1 << 16);
    CHECK(a && b && a != b, "two views at different addresses");
    if (a && b) {
        memcpy(a + 100, "shared", 7);
        CHECK(!memcmp(b + 100, "shared", 7), "a write through one view shows in the other");
        UnmapViewOfFile(a);
        UnmapViewOfFile(b);
    }
    CloseHandle(m);

    if (fails) {
        printf("FAIL: %d check(s)\n", fails);
        return 1;
    }
    puts("PASS: bionic branches (TerminateThread refuses without pthread_cancel; SuspendThread suspends; memfd via "
         "the system call)");
    return 0;
}
