/* compat_events - lost-wakeup check of the POSIX win32_compat.c events (2026-11-11): a waiter must be
 * released by SetEvent + an immediate ResetEvent, and by PulseEvent, manual- and auto-reset. Before the fix 0 of 200
 * woke in each case, after it 200 of 200. Build: clang -I src/platform -I src -I include tests/compat_events/evtest.c
 * src/platform/win32_compat.c -lpthread -lm */
#include "win32_compat.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
static HANDLE ev; static volatile int woke;
static void *waiter(void *a) { (void)a; woke = WaitForSingleObject(ev, 2000) == WAIT_OBJECT_0 ? 1 : -1; return NULL; }
static int trial(int manual, int mode) {   /* mode 0: Set+Reset, 1: Pulse */
    int ok = 0;
    for (int i = 0; i < 200; i++) {
        pthread_t t; woke = 0;
        ev = CreateEventA(NULL, manual, FALSE, NULL);
        pthread_create(&t, NULL, waiter, NULL);
        usleep(2000);                         /* the waiter is asleep in the wait */
        if (mode == 0) { SetEvent(ev); ResetEvent(ev); } else PulseEvent(ev);
        pthread_join(t, NULL);
        ok += woke == 1;
        CloseHandle(ev);
        if (i == 9 && ok == 0) return 0;      /* broken: stop early (each failure costs the 2 s timeout) */
    }
    return ok;
}
int main(void) {
    int r[4] = { trial(1, 0), trial(1, 1), trial(0, 0), trial(0, 1) }, fail = 0;
    printf("manual Set+Reset: %d/200 woke\n", r[0]);
    printf("manual Pulse:     %d/200 woke\n", r[1]);
    printf("auto   Set+Reset: %d/200 woke\n", r[2]);
    printf("auto   Pulse:     %d/200 woke\n", r[3]);
    for (int i = 0; i < 4; i++) fail |= r[i] != 200;
    /* The old behaviour that must stay - an auto-reset SetEvent with nobody waiting keeps the event signaled for
     * exactly one later wait, and a PulseEvent with nobody waiting leaves it reset. */
    ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    SetEvent(ev);
    int keep1 = WaitForSingleObject(ev, 0) == WAIT_OBJECT_0, keep2 = WaitForSingleObject(ev, 0) == WAIT_TIMEOUT;
    PulseEvent(ev);
    int pulse0 = WaitForSingleObject(ev, 0) == WAIT_TIMEOUT;
    CloseHandle(ev);
    printf("auto   Set without waiter: first wait %s, second %s; Pulse without waiter: %s\n",
           keep1 ? "ok" : "FAIL", keep2 ? "ok" : "FAIL", pulse0 ? "ok" : "FAIL");
    fail |= !keep1 || !keep2 || !pulse0;
    printf(fail ? "compat_events: FAIL\n" : "compat_events: PASS\n");
    return fail;
}
