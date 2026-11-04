/* The one-CPU gate (src/kernel/kernel_hal.c): a host-delivered ISR or DPC does not run while a guest thread holds
 * DISPATCH_LEVEL, a guest thread does not cross up to it while one runs, nesting works on both sides, an edge
 * event (vblank) that comes while the gate is closed is delivered exactly once afterwards, and RECOMP_DPC_GATE=0
 * turns it off. See README.md. Two ctest arms (the switch is read once per process). */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include <stdio.h>
#include <stdlib.h>

void *recomp_lookup(ULONG address) { (void)address; abort(); }
void *recomp_lookup_manual(ULONG address) { (void)address; abort(); }

static int fails;
static void check(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); fails++; }
}

/* A guest thread: raises, signals, waits to be told, lowers. */
static HANDLE s_raised, s_may_lower, s_lowered;
static DWORD WINAPI guest_raise_lower(LPVOID arg)
{
    X86Ctx guest = X86CTX_INIT;
    X86Ctx *ctx = &guest;
    guest.fs_base = 0;                          /* no guest memory here: nothing publishes fs:[0x24] */
    (void)arg;
    xbox_KfRaiseIrql(ctx, DISPATCH_LEVEL);
    xbox_KfRaiseIrql(ctx, 16);                  /* nested: still one holder */
    SetEvent(s_raised);
    WaitForSingleObject(s_may_lower, INFINITE);
    xbox_KfLowerIrql(ctx, DISPATCH_LEVEL);
    xbox_KfLowerIrql(ctx, PASSIVE_LEVEL);
    SetEvent(s_lowered);
    return 0;
}

/* A guest thread that raises while a host holds the gate: it must not get through before the host leaves. */
static volatile LONG s_through;
static DWORD WINAPI guest_raise_waits(LPVOID arg)
{
    X86Ctx guest = X86CTX_INIT;
    X86Ctx *ctx = &guest;
    guest.fs_base = 0;                          /* no guest memory here: nothing publishes fs:[0x24] */
    (void)arg;
    xbox_KfRaiseIrql(ctx, DISPATCH_LEVEL);
    InterlockedExchange(&s_through, 1);
    xbox_KfLowerIrql(ctx, PASSIVE_LEVEL);
    return 0;
}

int main(void)
{
    const char *e = getenv("GATE_EXPECT_ON");
    int expect_on = !(e && *e == '0');
    X86Ctx host = X86CTX_INIT;
    X86Ctx *ctx = &host;
    HANDLE t;
    volatile LONG vblank = 0;
    int delivered = 0, i;

    host.fs_base = 0;                           /* no guest memory here: nothing publishes fs:[0x24] */

    check(xbox_GateEnabled() == expect_on, "RECOMP_DPC_GATE decides whether the gate is on");

    /* 1. A guest thread at DISPATCH_LEVEL (raised twice) keeps a host deliverer out until its last lower, and a
     *    vblank that comes meanwhile is latched and delivered exactly once afterwards. */
    s_raised = CreateEventA(NULL, FALSE, FALSE, NULL);
    s_may_lower = CreateEventA(NULL, FALSE, FALSE, NULL);
    s_lowered = CreateEventA(NULL, FALSE, FALSE, NULL);
    t = CreateThread(NULL, 0, guest_raise_lower, NULL, 0, NULL);
    WaitForSingleObject(s_raised, INFINITE);
    InterlockedExchange(&vblank, 1);            /* the vblank comes while the guest is raised */
    InterlockedExchange(&vblank, 1);            /* and another: one status bit, one delivery */
    if (expect_on) {
        check(!xbox_GateTryEnter(), "a host ISR/DPC is deferred while a guest thread holds DISPATCH_LEVEL");
        check(!xbox_GateTakeEdge(&vblank), "the vblank is not taken while the guest is raised");
        check(vblank == 1, "the vblank stays latched");
    } else {
        check(xbox_GateTryEnter(), "gate off: granted while a guest thread is raised");
        xbox_GateLeave();
    }
    SetEvent(s_may_lower);
    WaitForSingleObject(s_lowered, INFINITE);
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    for (i = 0; i < 3; i++)                     /* the timer thread's next passes */
        if (xbox_GateTakeEdge(&vblank)) {
            delivered++;
            xbox_GateLeave();
        }
    check(delivered == 1, "the latched vblank is delivered exactly once after the lower");
    check(xbox_GateTryEnter(), "granted once the guest thread has lowered");

    /* 2. Nested on the host thread, and the deliverer's own raise (a DPC calling KfRaiseIrql) does not wait. */
    check(xbox_GateTryEnter(), "nested enter on the same host thread");
    xbox_KfRaiseIrql(ctx, DISPATCH_LEVEL);
    xbox_KfLowerIrql(ctx, PASSIVE_LEVEL);
    xbox_GateLeave();

    /* 3. While the host still holds it, a guest raise waits. */
    s_through = 0;
    t = CreateThread(NULL, 0, guest_raise_waits, NULL, 0, NULL);
    Sleep(100);
    if (expect_on)
        check(!s_through, "a guest thread does not cross up to DISPATCH_LEVEL while a host ISR/DPC runs");
    xbox_GateLeave();
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    check(s_through, "the guest thread gets through when the host leaves");

    /* 4. Left clean: the next host enter is granted at once. */
    check(xbox_GateTryEnter(), "the gate is free afterwards");
    xbox_GateLeave();

    if (fails)
        return 1;
    printf("PASS: one-CPU gate %s - deferral while raised (nested), vblank latched and delivered once, guest "
           "waits for a host, host nesting and own raises\n", expect_on ? "on" : "off");
    return 0;
}
