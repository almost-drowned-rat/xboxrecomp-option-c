/* compat_events - lost-wakeup check of the POSIX win32_compat.c events (2026-11-11), and of its
 * WaitForMultipleObjects (2026-11-15): a waiter must be released by SetEvent + an immediate ResetEvent, and by
 * PulseEvent, manual- and auto-reset - waiting on that one event, and waiting on it among others. Before the
 * single-wait fix 0 of 200 woke in each case; before the multi-wait fix a multi-object waiter never counted as an
 * event waiter (0 of 200 for an auto-reset event) and polled every 1 ms. Also: waitAll, timeouts, the wake-up
 * latency of a multi-object wait, an alertable one woken by QueueUserAPC, and SetThreadPriority's handle check.
 * Build: clang -I src/platform -I src -I include tests/compat_events/evtest.c src/platform/win32_compat.c
 * -lpthread -lm */
#include "win32_compat.h"
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

static HANDLE ev, other; static volatile int woke; static int multi;

static void *waiter(void *a)
{
    (void)a;
    if (multi) {
        HANDLE h[2] = { other, ev };
        DWORD r = WaitForMultipleObjects(2, h, FALSE, 2000);
        woke = r == WAIT_OBJECT_0 + 1 ? 1 : -1;
    } else {
        woke = WaitForSingleObject(ev, 2000) == WAIT_OBJECT_0 ? 1 : -1;
    }
    return NULL;
}

static int trial(int manual, int mode)       /* mode 0: Set+Reset, 1: Pulse */
{
    int ok = 0;
    for (int i = 0; i < 200; i++) {
        pthread_t t; woke = 0;
        ev = CreateEventA(NULL, manual, FALSE, NULL);
        other = CreateEventA(NULL, FALSE, FALSE, NULL);
        pthread_create(&t, NULL, waiter, NULL);
        usleep(2000);                         /* the waiter is asleep in the wait */
        if (mode == 0) { SetEvent(ev); ResetEvent(ev); } else PulseEvent(ev);
        pthread_join(t, NULL);
        ok += woke == 1;
        CloseHandle(ev);
        CloseHandle(other);
        if (i == 9 && ok == 0) return 0;      /* broken: stop early (each failure costs the 2 s timeout) */
    }
    return ok;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static volatile double s_set_at, s_woke_at;
static void *latency_waiter(void *a)
{
    HANDLE h[2] = { other, ev };
    (void)a;
    if (WaitForMultipleObjects(2, h, FALSE, 2000) == WAIT_OBJECT_0 + 1)
        s_woke_at = now_ms();
    return NULL;
}

static volatile int s_apc_ran;
static void CALLBACK apc(ULONG_PTR d) { (void)d; s_apc_ran = 1; }
static volatile DWORD s_alert_result;
static DWORD WINAPI alert_waiter(LPVOID a)
{
    HANDLE h[2] = { ev, other };
    (void)a;
    s_alert_result = WaitForMultipleObjectsEx(2, h, FALSE, 2000, TRUE);
    return 0;
}

int main(void)
{
    int fail = 0, r[8];
    for (multi = 0; multi < 2; multi++) {
        int *q = r + 4 * multi;
        q[0] = trial(1, 0); q[1] = trial(1, 1); q[2] = trial(0, 0); q[3] = trial(0, 1);
        printf("%s manual Set+Reset: %d/200 woke\n", multi ? "multi " : "single", q[0]);
        printf("%s manual Pulse:     %d/200 woke\n", multi ? "multi " : "single", q[1]);
        printf("%s auto   Set+Reset: %d/200 woke\n", multi ? "multi " : "single", q[2]);
        printf("%s auto   Pulse:     %d/200 woke\n", multi ? "multi " : "single", q[3]);
    }
    for (int i = 0; i < 8; i++) fail |= r[i] != 200;

    /* The old behaviour that must stay - an auto-reset SetEvent with nobody waiting keeps the event signaled for
     * exactly one later wait, and a PulseEvent with nobody waiting leaves it reset. */
    ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    SetEvent(ev);
    int keep1 = WaitForSingleObject(ev, 0) == WAIT_OBJECT_0, keep2 = WaitForSingleObject(ev, 0) == WAIT_TIMEOUT;
    PulseEvent(ev);
    int pulse0 = WaitForSingleObject(ev, 0) == WAIT_TIMEOUT;
    printf("auto   Set without waiter: first wait %s, second %s; Pulse without waiter: %s\n",
           keep1 ? "ok" : "FAIL", keep2 ? "ok" : "FAIL", pulse0 ? "ok" : "FAIL");
    fail |= !keep1 || !keep2 || !pulse0;

    /* the same through a multi-object wait: a set auto-reset event is taken by exactly one wait */
    other = CreateEventA(NULL, FALSE, FALSE, NULL);
    {
        HANDLE h[2] = { other, ev };
        SetEvent(ev);
        int m1 = WaitForMultipleObjects(2, h, FALSE, 0) == WAIT_OBJECT_0 + 1;
        int m2 = WaitForMultipleObjects(2, h, FALSE, 0) == WAIT_TIMEOUT;
        double t0 = now_ms();
        int m3 = WaitForMultipleObjects(2, h, FALSE, 50) == WAIT_TIMEOUT;
        double waited = now_ms() - t0;
        /* waitAll: nothing taken until both are set, then both are */
        SetEvent(ev);
        int a1 = WaitForMultipleObjects(2, h, TRUE, 0) == WAIT_TIMEOUT;
        int a2 = WaitForSingleObject(ev, 0) == WAIT_OBJECT_0;     /* still set: waitAll took nothing */
        SetEvent(ev); SetEvent(other);
        int a3 = WaitForMultipleObjects(2, h, TRUE, 0) == WAIT_OBJECT_0;
        int a4 = WaitForSingleObject(ev, 0) == WAIT_TIMEOUT && WaitForSingleObject(other, 0) == WAIT_TIMEOUT;
        printf("multi  set taken once %s/%s, 50 ms timeout %s (%.1f ms), waitAll none %s/%s, both %s/%s\n",
               m1 ? "ok" : "FAIL", m2 ? "ok" : "FAIL", m3 && waited >= 49.0 ? "ok" : "FAIL", waited,
               a1 ? "ok" : "FAIL", a2 ? "ok" : "FAIL", a3 ? "ok" : "FAIL", a4 ? "ok" : "FAIL");
        fail |= !m1 || !m2 || !m3 || waited < 49.0 || !a1 || !a2 || !a3 || !a4;
    }

    /* wake-up latency of a multi-object wait (the old poll: up to 1 ms + usleep slack); median of 50 */
    {
        double lat[50], med;
        int n = 0;
        for (int i = 0; i < 50; i++) {
            pthread_t t;
            s_woke_at = 0;
            pthread_create(&t, NULL, latency_waiter, NULL);
            usleep(3000);
            s_set_at = now_ms();
            SetEvent(ev);
            pthread_join(t, NULL);
            if (s_woke_at > 0) lat[n++] = s_woke_at - s_set_at;
        }
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && lat[j - 1] > lat[j]; j--) { double x = lat[j]; lat[j] = lat[j - 1]; lat[j - 1] = x; }
        med = n ? lat[n / 2] : 99.0;
        printf("multi  wake-up latency: median %.3f ms of %d (limit 0.5 ms)\n", med, n);
        fail |= n != 50 || med > 0.5;
    }

    /* an alertable multi-object wait returns WAIT_IO_COMPLETION for an APC queued while it sleeps, and ran it */
    {
        HANDLE t = CreateThread(NULL, 0, alert_waiter, NULL, 0, NULL);
        usleep(20000);
        QueueUserAPC(apc, t, 0);
        WaitForSingleObject(t, 3000);
        printf("multi  alertable + APC: %s, APC %s\n", s_alert_result == WAIT_IO_COMPLETION ? "ok" : "FAIL",
               s_apc_ran ? "ran" : "FAIL");
        fail |= s_alert_result != WAIT_IO_COMPLETION || !s_apc_ran;
        CloseHandle(t);
    }

    /* SetThreadPriority: the pseudo handle and a thread object work, anything else is refused (the kernel bridge
     * once passed a guest KTHREAD pointer, which was read as a thread object) */
    {
        static int not_a_thread[64];
        int p1 = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL) &&
                 GetThreadPriority(GetCurrentThread()) == THREAD_PRIORITY_NORMAL;
        int p2 = !SetThreadPriority((HANDLE)not_a_thread, THREAD_PRIORITY_HIGHEST);
        int p3 = !SetThreadPriority(ev, THREAD_PRIORITY_HIGHEST);
        printf("SetThreadPriority: current %s, foreign pointer refused %s, event refused %s\n",
               p1 ? "ok" : "FAIL", p2 ? "ok" : "FAIL", p3 ? "ok" : "FAIL");
        fail |= !p1 || !p2 || !p3;
    }
    CloseHandle(other);
    CloseHandle(ev);

    printf(fail ? "compat_events: FAIL\n" : "compat_events: PASS\n");
    return fail;
}
