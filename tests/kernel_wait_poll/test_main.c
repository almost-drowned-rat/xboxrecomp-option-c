/*
 * Zero-timeout polls in a tight loop are throttled (kernel_sync.c wait_poll_note; 2026-11-01).
 *
 * Panzer Dragoon Orta's loader thread loops WaitForSingleObjectEx(event, 0, TRUE) - ~2.3 million times a second, a
 * host core at 100 %. Checked on the kernel's own waits:
 *   1. 6,400 zero-timeout waits on an unsignalled event in a tight loop take at least ~100 host sleeps of 1 ms
 *      (one per 64 polls), i.e. >= ~90 ms instead of a few ms;
 *   2. a loop that does ~0.1 ms of work between its polls is not slowed (the window is 64 polls in 2 ms);
 *   3. a satisfied wait answers at once and resets the count.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "platform/host_wait.h"

#include <stdio.h>

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-62s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

int main(void)
{
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    LARGE_INTEGER zero;
    uint64_t t0, dt;
    char d[128];
    int i;
    NTSTATUS st = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    zero.QuadPart = 0;

    /* 1 */
    t0 = host_time_ns();
    for (i = 0; i < 6400; i++)
        st = xbox_NtWaitForSingleObjectEx(ev, 1, TRUE, &zero);
    dt = host_time_ns() - t0;
    snprintf(d, sizeof d, "%.1f ms, last status 0x%08X", dt / 1e6, (unsigned)st);
    check(st == STATUS_TIMEOUT && dt >= 90000000ull, "tight zero-timeout loop: throttled (>= 90 ms for 6,400)", d);

    /* 2: ~0.1 ms of work between polls -> 64 polls take ~6.4 ms > the 2 ms window */
    t0 = host_time_ns();
    for (i = 0; i < 200; i++) {
        uint64_t w = host_time_ns();
        while (host_time_ns() - w < 100000ull)
            ;
        xbox_NtWaitForSingleObjectEx(ev, 1, TRUE, &zero);
    }
    dt = host_time_ns() - t0;
    snprintf(d, sizeof d, "%.1f ms for 200 x 0.1 ms of work", dt / 1e6);
    check(dt < 35000000ull, "a polling loop that works between polls is not slowed", d);

    /* 3 */
    SetEvent(ev);
    t0 = host_time_ns();
    st = xbox_NtWaitForSingleObjectEx(ev, 1, TRUE, &zero);
    dt = host_time_ns() - t0;
    snprintf(d, sizeof d, "status 0x%08X in %.3f ms", (unsigned)st, dt / 1e6);
    check(st == STATUS_SUCCESS && dt < 1000000ull, "a satisfied wait answers at once", d);

    CloseHandle(ev);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
