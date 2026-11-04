/*
 * Guest thread objects: what ObReferenceObjectByHandle hands back for a thread handle (2026-11-01).
 *
 * XAPI's GetExitCodeThread references the handle's object and reads Header.SignalState (+4) and ExitStatus
 * (+0x120). The bridge returned NULL, and Panzer Dragoon Orta's CRT, polling a movie thread that had ended,
 * read [0+4] on the main thread. Runs the real memory layout on the small synthetic XBE from tools/conformance
 * (as tests/kernel_events), spawns real worker threads through PsCreateSystemThreadEx in SPAWN mode, and checks:
 *   1. a running thread's object: non-NULL, ThreadObject type, SignalState 0, ExitStatus STILL_ACTIVE;
 *   2. its routine returns: SignalState 1, ExitStatus = the routine's eax;
 *   3. a thread ending through PsTerminateSystemThread: ExitStatus = its argument (0xF0000001 as in Orta);
 *   4. a handle that is no spawned thread keeps the old answer (NULL, STATUS_SUCCESS).
 * Every call also checks the stdcall stack cleanup.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROUTINE_RETURNS    0x00401000u
#define ROUTINE_TERMINATES 0x00401100u
#define RETURN_VALUE       0x00001234u
#define TERMINATE_STATUS   0xF0000001u

#define ROUTINE_RT         0x00401200u
#define RT_OUT_VA_OFF      0x2020u

enum { S_CREATE, S_TERMINATE, S_REFERENCE, S_CLOSE, S_DEREF, S_SETPRIO, N_SLOTS };
static const uint32_t ORD[N_SLOTS] = {
    255,   /* PsCreateSystemThreadEx */
    258,   /* PsTerminateSystemThread */
    246,   /* ObReferenceObjectByHandle */
    187,   /* NtClose */
    250,   /* ObfDereferenceObject (fastcall: ecx) */
    143,   /* KeSetBasePriorityThread */
};
static volatile LONG s_rt_phase;         /* the time-critical routine: 1 raised, 2 lowering, 3 normal again */
static uint32_t s_scratch_va;            /* scratch, for the routine (set in main) */
static uint32_t slot_va[N_SLOTS];
static HANDLE s_go;                      /* the first routine waits for it, so it can be seen running */

extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);

/* Guest thread routines (host functions the stand-in recomp_lookup hands out); `ctx` is the worker's own. */
static void routine_returns(RECOMP_PARAMS)
{
    (void)base;
    WaitForSingleObject(s_go, 10000);
    g_eax = RETURN_VALUE;
    g_esp += 4;                          /* the dummy return address; ctx1/ctx2 are the caller's (bridge's) */
}

static void routine_terminates(RECOMP_PARAMS)
{
    uint32_t *sp;
    (void)base;
    g_esp -= 8;
    sp = (uint32_t *)((uintptr_t)g_esp + xbox_GetMemoryOffset());
    sp[0] = 0xBEEF0002u;                 /* return address */
    sp[1] = TERMINATE_STATUS;
    RECOMP_CALL_GUEST(recomp_lookup_kernel(slot_va[S_TERMINATE]));   /* does not return: ExitThread */
}

/* A stdcall kernel call from inside a routine, on its own registers. */
static void routine_kcall(X86Ctx *ctx, int slot, int nargs, const uint32_t *args)
{
    uint32_t *sp;
    int i;
    g_esp -= 4u * (uint32_t)(nargs + 1);
    sp = (uint32_t *)((uintptr_t)g_esp + xbox_GetMemoryOffset());
    sp[0] = 0xBEEF0003u;
    for (i = 0; i < nargs; i++)
        sp[1 + i] = args[i];
    RECOMP_CALL_GUEST(recomp_lookup_kernel(slot_va[slot]));
}

/* SetThreadPriority(GetCurrentThread(), TIME_CRITICAL) as XAPI does it, 300 ms of guest work without a kernel
 * call, then back to normal. */
static void routine_rt(RECOMP_PARAMS)
{
    uint32_t a[3], obj;
    (void)base;
    a[0] = 0xFFFFFFFEu; a[1] = 0; a[2] = s_scratch_va + RT_OUT_VA_OFF;    /* NtCurrentThread() */
    routine_kcall(ctx, S_REFERENCE, 3, a);
    obj = *(uint32_t *)((uintptr_t)(s_scratch_va + RT_OUT_VA_OFF) + xbox_GetMemoryOffset());
    a[0] = obj; a[1] = 16;
    routine_kcall(ctx, S_SETPRIO, 2, a);
    InterlockedExchange(&s_rt_phase, obj ? 1 : -1);
    Sleep(300);                          /* guest code: no kernel call */
    InterlockedExchange(&s_rt_phase, 2);
    a[0] = obj; a[1] = 0;
    routine_kcall(ctx, S_SETPRIO, 2, a);
    InterlockedExchange(&s_rt_phase, 3);
    g_eax = 0;
    g_esp += 4;
}

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va)
{
    return xbox_va == ROUTINE_RETURNS ? routine_returns
         : xbox_va == ROUTINE_TERMINATES ? routine_terminates
         : xbox_va == ROUTINE_RT ? routine_rt : NULL;
}
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

/* From here on the test's own register set. */
static X86Ctx s_test_ctx = X86CTX_INIT;
#define ctx (&s_test_ctx)

static uint32_t scratch;
#define THUNK_VA  (scratch)
#define STACK_VA  (scratch + 0x1000)
#define HANDLE_VA (scratch + 0x2000)
#define OUT_VA    (scratch + 0x2010)

static int failures;

static uint32_t *G(uint32_t va) { return (uint32_t *)((uintptr_t)va + xbox_GetMemoryOffset()); }

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-66s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

static uint32_t call(int slot, int nargs, const uint32_t *args)
{
    uint32_t *sp = G(STACK_VA);
    int i;
    sp[0] = 0xBEEF0001u;
    for (i = 0; i < nargs; i++)
        sp[1 + i] = args[i];
    g_esp = STACK_VA;
    g_eax = 0xDEADBEEFu;
    RECOMP_CALL_GUEST(recomp_lookup_kernel(slot_va[slot]));
    check(g_esp == STACK_VA + 4u + 4u * (uint32_t)nargs, "stdcall stack cleanup", NULL);
    return g_eax;
}

static uint32_t spawn(uint32_t routine)
{
    uint32_t a[10] = { HANDLE_VA, 0, 0, 0, 0, 0x11u, 0x22u, 0, 0, routine };
    *G(HANDLE_VA) = 0;
    call(S_CREATE, 10, a);
    return *G(HANDLE_VA);
}

static uint32_t reference(uint32_t token, uint32_t *status)
{
    uint32_t a[3] = { token, 0, OUT_VA };
    *G(OUT_VA) = 0xCCCCCCCCu;
    *status = call(S_REFERENCE, 3, a);
    return *G(OUT_VA);
}

static int wait_signalled(uint32_t obj)
{
    int i;
    for (i = 0; i < 500; i++) {
        if (G(obj + 4)[0] == 1)
            return 1;
        Sleep(10);
    }
    return 0;
}

static unsigned char *load_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tools/conformance/test.xbe";
    size_t xbe_len = 0;
    unsigned char *xbe;
    uint32_t token, obj, st;
    char d[160];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    xbe = load_file(path, &xbe_len);
    if (!xbe) { printf("cannot read %s -- run: python tools/conformance/mkxbe.py\n", path); return 2; }
    if (!xbox_MemoryLayoutInit(xbe, xbe_len)) { puts("xbox_MemoryLayoutInit failed"); return 2; }

    scratch = xbox_HeapAlloc(0x10000, 4096);
    for (i = 0; i < N_SLOTS; i++)
        *G(THUNK_VA + 4 * i) = 0x80000000u | ORD[i];
    xbox_kernel_set_thunk_address(THUNK_VA, N_SLOTS);
    xbox_kernel_bridge_init();
    for (i = 0; i < N_SLOTS; i++) {
        slot_va[i] = *G(THUNK_VA + 4 * i);
        if (!recomp_lookup_kernel(slot_va[i])) { printf("ordinal %u did not resolve\n", ORD[i]); return 2; }
    }
    xbox_SetThreadMode(XBOX_THREAD_MODE_SPAWN);       /* every PsCreateSystemThreadEx spawns a real thread */
    s_go = CreateEventA(NULL, TRUE, FALSE, NULL);

    /* 1 + 2: a thread whose routine returns. */
    token = spawn(ROUTINE_RETURNS);
    snprintf(d, sizeof d, "token 0x%08X", token);
    check(token != 0, "PsCreateSystemThreadEx wrote a handle", d);
    obj = reference(token, &st);
    snprintf(d, sizeof d, "status 0x%08X object 0x%08X", st, obj);
    check(st == 0 && obj != 0 && obj != 0xCCCCCCCCu, "running thread: ObReferenceObjectByHandle gives an object", d);
    if (obj && obj != 0xCCCCCCCCu) {
        snprintf(d, sizeof d, "type %u SignalState %u ExitStatus 0x%08X", *(uint8_t *)G(obj), G(obj)[1],
                 G(obj + 0x120)[0]);
        check(*(uint8_t *)G(obj) == 6 && G(obj)[1] == 0 && G(obj + 0x120)[0] == 0x103u,
              "running thread: ThreadObject, not signalled, STILL_ACTIVE", d);
        SetEvent(s_go);
        check(wait_signalled(obj), "routine returned: SignalState 1", NULL);
        snprintf(d, sizeof d, "ExitStatus 0x%08X", G(obj + 0x120)[0]);
        check(G(obj + 0x120)[0] == RETURN_VALUE, "routine returned: ExitStatus = its eax", d);
        check(reference(token, &st) == obj, "the same handle gives the same object after the end", NULL);
    }

    /* 3: a thread that ends through PsTerminateSystemThread. */
    token = spawn(ROUTINE_TERMINATES);
    obj = reference(token, &st);
    snprintf(d, sizeof d, "status 0x%08X object 0x%08X", st, obj);
    check(st == 0 && obj != 0 && obj != 0xCCCCCCCCu, "terminating thread: an object", d);
    if (obj && obj != 0xCCCCCCCCu) {
        check(wait_signalled(obj), "PsTerminateSystemThread: SignalState 1", NULL);
        snprintf(d, sizeof d, "ExitStatus 0x%08X", G(obj + 0x120)[0]);
        check(G(obj + 0x120)[0] == TERMINATE_STATUS, "PsTerminateSystemThread: ExitStatus = its argument", d);
    }

    /* 4: anything else keeps the old answer. */
    obj = reference(0x48000FFFu, &st);
    snprintf(d, sizeof d, "status 0x%08X object 0x%08X", st, obj);
    check(st == 0 && obj == 0, "a handle that is no spawned thread: NULL, STATUS_SUCCESS (as before)", d);

    /* 5: lifetime. The object outlives the thread and the handle while referenced, and goes with the last
     * dereference; a title that respawns a worker for ever (Panzer Dragoon Orta, ~1/s) never runs the table
     * (256) out. Each round: spawn, reference, end, close the handle, dereference - as GetExitCodeThread and
     * CloseHandle do. */
    {
        int r, all_obj = 1, all_kept = 1, all_gone = 1;
        for (r = 0; r < 300; r++) {
            uint32_t a1[1], o;
            token = spawn(ROUTINE_TERMINATES);
            o = reference(token, &st);
            if (!o || o == 0xCCCCCCCCu) { all_obj = 0; break; }
            if (!wait_signalled(o)) { all_obj = 0; break; }
            a1[0] = token;
            call(S_CLOSE, 1, a1);
            if (*(uint8_t *)G(o) != 6 || G(o + 0x120)[0] != TERMINATE_STATUS)
                all_kept = 0;                        /* still referenced: still readable */
            g_ecx = o;
            call(S_DEREF, 0, NULL);
            if (*(uint8_t *)G(o) == 6)
                all_gone = 0;                        /* last reference gone: released */
        }
        snprintf(d, sizeof d, "rounds %d", r);
        check(all_obj && r == 300, "300 spawn/reference/close/dereference rounds: an object every time", d);
        check(all_kept, "closed but referenced: the object stays readable", NULL);
        check(all_gone, "last dereference after end and close: the object is released", NULL);
    }

    /* 6: the time-critical gate. A worker raises itself to TIME_CRITICAL through NtCurrentThread() and works
     * 300 ms without a kernel call; this (guest) thread's kernel calls pause meanwhile - at most 100 ms each -
     * and are quick again once the worker is back to normal. */
    {
        extern void guest_cpu_join(void);
        LARGE_INTEGER f, t0, t1;
        double during_ms = 0, after_ms;
        int k;
#ifdef _WIN32
        _putenv("RECOMP_RT_GATE=1");                     /* off by default */
#else
        setenv("RECOMP_RT_GATE", "1", 1);
#endif
        guest_cpu_join();                                /* this thread counts as a guest thread from here on */
        s_scratch_va = scratch;
        QueryPerformanceFrequency(&f);
        spawn(ROUTINE_RT);
        for (k = 0; k < 500 && s_rt_phase == 0; k++)
            Sleep(2);
        snprintf(d, sizeof d, "phase %ld", (long)s_rt_phase);
        check(s_rt_phase == 1, "NtCurrentThread(): an object, raised to TIME_CRITICAL", d);
        QueryPerformanceCounter(&t0);
        reference(0x48000FFFu, &st);                    /* any kernel call */
        QueryPerformanceCounter(&t1);
        during_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
        snprintf(d, sizeof d, "%.1f ms", during_ms);
        check(during_ms >= 50.0, "while it runs: this thread's kernel call pauses", d);
        for (k = 0; k < 500 && s_rt_phase != 3; k++)
            Sleep(2);
        QueryPerformanceCounter(&t0);
        reference(0x48000FFFu, &st);
        QueryPerformanceCounter(&t1);
        after_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
        snprintf(d, sizeof d, "%.1f ms", after_ms);
        check(s_rt_phase == 3 && after_ms < 20.0, "back to normal: kernel calls are quick again", d);
    }

    printf("\n%d failure(s)\n", failures);
    fflush(stdout);
    ExitProcess(failures ? 1 : 0);       /* worker threads may still sit in the runtime's timers */
}
