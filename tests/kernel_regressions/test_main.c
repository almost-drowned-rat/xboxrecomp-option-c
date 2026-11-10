/*
 * Kernel regressions: fixes, each checked against the bug it fixed (6: concurrent handle tokens, see race_check).
 *
 * Like tests/kernel_bridge, this calls the kernel through the thunk dispatcher
 * -- the path a title's kernel calls take -- with a plain buffer standing in
 * for guest memory. No title and no game files.
 *
 * Each check fails without its fix and passes with it:
 *
 *   1. STATUS_CONFLICTING_ADDRESSES (0xC0000018) maps to ERROR_INVALID_ADDRESS
 *      (487), in both places the mapping exists (the bridge's ordinal 301 and
 *      xbox_RtlNtStatusToDosError). 487 is the only error on which the MSVC
 *      CRT heap tries another address when it grows; the generic 317 made it
 *      stop growing.
 *
 *   2. NtCurrentThread() (-2) and NtCurrentProcess() (-1) reach Win32 as
 *      pseudo-handles. Zero-extended on a 64-bit host they are not, and
 *      DuplicateHandle rejects them, so NtDuplicateObject failed.
 *
 *   3. The kernel call counter does not wrap at 2^31. The log gate is "count
 *      <= budget", which a wrapped negative count satisfies, so every call
 *      after the wrap wrote log lines. The counter is private to the bridge,
 *      so the check drives 2^31 + 200000 calls of a cheap ordinal and counts
 *      the "[KERNEL] #" lines written. Slow (tens of seconds), so it only runs
 *      when the program is given the argument "wrap" (see CMakeLists.txt).
 */
#include "kernel.h"
#include "xbox_memory_layout.h"   /* RECOMP_TLS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Option C: this host test owns one guest register set; g_eax, g_esp and
 * RECOMP_CALL_GUEST reach it through `ctx` like everywhere else. */
static X86Ctx s_test_ctx = X86CTX_INIT;
#define ctx (&s_test_ctx)


/* Provided by the generated title; this harness has none. Nothing here calls a
 * guest function. */
/* recomp_func_t: void(X86Ctx *ctx, uint8_t *base) - xbox_memory_layout.h / recomp_types.h */
recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);
extern ptrdiff_t g_xbox_mem_offset;

#define THUNK_VA 0x10000u
#define STACK_VA 0x20000u
#define OUT_VA   0x30000u

/* The ordinals used; thunk slot i is at THUNK_VA + 4*i. */
enum { S_MMGPA, S_DUP, S_STATUS2DOS, S_CREATE_EVENT, S_CLOSE, S_SET_EVENT, N_SLOTS };
static const uint32_t ORD[N_SLOTS] = {
    173,   /* MmGetPhysicalAddress (cheap; used to drive the counter) */
    197,   /* NtDuplicateObject */
    301,   /* RtlNtStatusToDosError */
    189,   /* NtCreateEvent */
    187,   /* NtClose */
    225,   /* NtSetEvent */
};

static uint8_t *mem;
static uint32_t slot_va[N_SLOTS];
static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-62s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

/* recomp_lookup_kernel() selects the slot when it is called, so resolve right
 * before every call instead of caching the returned pointer. */
static recomp_func_t resolve(int slot)
{
    return recomp_lookup_kernel(slot_va[slot]);
}

/* stdcall call through the dispatcher; returns eax. */
static uint32_t call(int slot, int nargs, const uint32_t *args)
{
    uint32_t *sp = (uint32_t *)(mem + STACK_VA);
    int i;

    sp[0] = 0xBEEF0001u;                 /* guest return address */
    for (i = 0; i < nargs; i++)
        sp[1 + i] = args[i];
    g_esp = STACK_VA;
    g_eax = 0xDEADBEEFu;
    RECOMP_CALL_GUEST(resolve(slot));
    return g_eax;
}

/* 6. Handle tokens created on several threads at once (Panzer Dragoon Orta issue #1 stage 7: two threads took the
 *    same free table slot, one closed its event and the other's token went dead). Each thread has its own register
 *    set and guest stack, creates RACE_EVENTS events per round and keeps them; after every round all live tokens
 *    must be different and resolve to different host handles, then each thread closes its own. */
#define RACE_THREADS 8
#define RACE_EVENTS  24
#define RACE_ROUNDS  150
extern void *xbox_bridge_host_handle(uint32_t token);
static uint32_t s_race_tok[RACE_THREADS][RACE_EVENTS];
static HANDLE s_race_go[RACE_THREADS], s_race_done[RACE_THREADS];
static volatile LONG s_race_stop;

static uint32_t call_on(X86Ctx *c, uint32_t stack_va, int slot, int nargs, const uint32_t *args)
{
    uint32_t *sp = (uint32_t *)(mem + stack_va);
    int i;
    sp[0] = 0xBEEF0001u;
    for (i = 0; i < nargs; i++)
        sp[1 + i] = args[i];
    c->r_esp = stack_va;
    c->r_eax = 0xDEADBEEFu;
    resolve(slot)(c, RECOMP_GUEST_BASE);
    return c->r_eax;
}

static DWORD WINAPI race_thread(LPVOID arg)
{
    int t = (int)(intptr_t)arg, k;
    X86Ctx c = X86CTX_INIT;
    uint32_t area = 0x100000u + 0x10000u * (uint32_t)t, out = area + 0x100, stack = area + 0x8000;
    for (;;) {
        WaitForSingleObject(s_race_go[t], INFINITE);
        if (s_race_stop)
            return 0;
        for (k = 0; k < RACE_EVENTS; k++) {            /* close last round's, create this round's */
            uint32_t a[4] = { out, 0, 1, 0 };
            if (s_race_tok[t][k]) { uint32_t h = s_race_tok[t][k]; call_on(&c, stack, S_CLOSE, 1, &h); }
            call_on(&c, stack, S_CREATE_EVENT, 4, a);
            s_race_tok[t][k] = *(uint32_t *)(mem + out);
        }
        SetEvent(s_race_done[t]);
    }
}

static int race_check(char *d, size_t dn)
{
    int t, u, v, r;
    for (t = 0; t < RACE_THREADS; t++) {
        s_race_go[t] = CreateEventW(NULL, FALSE, FALSE, NULL);
        s_race_done[t] = CreateEventW(NULL, FALSE, FALSE, NULL);
        CloseHandle(CreateThread(NULL, 0, race_thread, (LPVOID)(intptr_t)t, 0, NULL));
    }
    snprintf(d, dn, "%d rounds of %d threads x %d events: every token separate", RACE_ROUNDS, RACE_THREADS,
             RACE_EVENTS);
    for (r = 0; r < RACE_ROUNDS; r++) {
        for (t = 0; t < RACE_THREADS; t++) SetEvent(s_race_go[t]);
        WaitForMultipleObjects(RACE_THREADS, s_race_done, TRUE, INFINITE);
        for (u = 0; u < RACE_THREADS * RACE_EVENTS; u++) {
            uint32_t a = s_race_tok[u / RACE_EVENTS][u % RACE_EVENTS];
            void *ha = xbox_bridge_host_handle(a);
            if (!a || !ha) {
                snprintf(d, dn, "round %d: token 0x%08X resolves to nothing", r, a);
                goto bad;
            }
            for (v = u + 1; v < RACE_THREADS * RACE_EVENTS; v++) {
                uint32_t b = s_race_tok[v / RACE_EVENTS][v % RACE_EVENTS];
                if (a == b || ha == xbox_bridge_host_handle(b)) {
                    snprintf(d, dn, "round %d: two live events share token 0x%08X / host %p", r, a, ha);
                    goto bad;
                }
            }
        }
    }
    s_race_stop = 1;
    for (t = 0; t < RACE_THREADS; t++) SetEvent(s_race_go[t]);
    return 1;
bad:
    s_race_stop = 1;
    for (t = 0; t < RACE_THREADS; t++) SetEvent(s_race_go[t]);
    return 0;
}

int main(int argc, char **argv)
{
    char d[160];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);

    mem = VirtualAlloc(NULL, 16 * 1024 * 1024, MEM_RESERVE | MEM_COMMIT,
                       PAGE_READWRITE);
    if (!mem) {
        puts("could not allocate the guest memory stand-in");
        return 2;
    }
    g_xbox_mem_offset = (ptrdiff_t)mem;

    for (i = 0; i < N_SLOTS; i++)
        *(uint32_t *)(mem + THUNK_VA + 4 * i) = 0x80000000u | ORD[i];
    xbox_kernel_set_thunk_address(THUNK_VA, N_SLOTS);
    xbox_kernel_bridge_init();
    for (i = 0; i < N_SLOTS; i++) {
        slot_va[i] = *(uint32_t *)(mem + THUNK_VA + 4 * i);
        if (!resolve(i)) {
            printf("ordinal %u did not resolve to a bridge entry point\n", ORD[i]);
            return 2;
        }
    }

    /* 1. STATUS_CONFLICTING_ADDRESSES -> 487, in both places. */
    {
        ULONG r = xbox_RtlNtStatusToDosError((NTSTATUS)0xC0000018u);
        uint32_t arg = 0xC0000018u;
        uint32_t b = call(S_STATUS2DOS, 1, &arg);

        snprintf(d, sizeof d, "returned %lu, expected 487", (unsigned long)r);
        check(r == 487, "xbox_RtlNtStatusToDosError(0xC0000018) == 487", d);
        snprintf(d, sizeof d, "returned %u, expected 487", b);
        check(b == 487, "bridge RtlNtStatusToDosError(0xC0000018) == 487", d);
    }

    /* 2. Pseudo-handles duplicate. */
    {
        static const uint32_t token[2] = { 0xFFFFFFFEu, 0xFFFFFFFFu };
        static const char *const what[2] = {
            "NtDuplicateObject(NtCurrentThread()) succeeds",
            "NtDuplicateObject(NtCurrentProcess()) succeeds",
        };

        for (i = 0; i < 2; i++) {
            uint32_t args[3];
            uint32_t st;

            args[0] = token[i];
            args[1] = OUT_VA;
            args[2] = 0x2;                       /* DUPLICATE_SAME_ACCESS */
            *(uint32_t *)(mem + OUT_VA) = 0;
            st = call(S_DUP, 3, args);
            snprintf(d, sizeof d, "status 0x%08X, handle 0x%08X", st,
                     *(uint32_t *)(mem + OUT_VA));
            check(st == 0 && *(uint32_t *)(mem + OUT_VA) != 0, what[i], d);
        }
    }

    /* 4. Handle tokens after NtDuplicateObject(DUPLICATE_CLOSE_SOURCE) (2026-11-01). The host closes
     *    the source handle and Windows commonly hands its number out again at once. The source token used to stay
     *    in the table pointing at that number, so the next new object was given the same token (Panzer Dragoon
     *    Orta's movie thread got an older event's token, and closing one closed the other). Now: the duplicate
     *    gets its own token, the source token resolves to nothing, and the next new object is separate from the
     *    duplicate, both staying valid. Repeated, because the number reuse is up to Windows. */
    {
        uint32_t ev_args[4] = { OUT_VA, 0, 1, 0 };          /* NtCreateEvent(&h, NULL, sync, not set) */
        uint32_t e1 = 0, e2 = 0, dup = 0, dup_args[3], set_args[2], st_old, st_dup, st_new;
        int k, bad = 0;

        set_args[1] = 0;
        for (k = 0; k < 16 && !bad; k++) {
            call(S_CREATE_EVENT, 4, ev_args);
            e1 = *(uint32_t *)(mem + OUT_VA);
            dup_args[0] = e1; dup_args[1] = OUT_VA + 8; dup_args[2] = 0x3;   /* CLOSE_SOURCE | SAME_ACCESS */
            call(S_DUP, 3, dup_args);
            dup = *(uint32_t *)(mem + OUT_VA + 8);
            set_args[0] = e1;  st_old = call(S_SET_EVENT, 2, set_args);   /* closed: must fail */
            call(S_CREATE_EVENT, 4, ev_args);
            e2 = *(uint32_t *)(mem + OUT_VA);
            set_args[0] = dup; st_dup = call(S_SET_EVENT, 2, set_args);
            set_args[0] = e2;  st_new = call(S_SET_EVENT, 2, set_args);
            if (dup == e1 || e2 == dup || (int32_t)st_old >= 0 || (int32_t)st_dup < 0 || (int32_t)st_new < 0)
                bad = 1;
            snprintf(d, sizeof d, "round %d: source 0x%08X (set 0x%08X), duplicate 0x%08X (set 0x%08X), "
                     "next 0x%08X (set 0x%08X)", k, e1, st_old, dup, st_dup, e2, st_new);
            { uint32_t c = dup; call(S_CLOSE, 1, &c); c = e2; call(S_CLOSE, 1, &c); }
        }
        check(!bad, "CLOSE_SOURCE: source token dead, duplicate and next object separate", d);
    }

    /* 5. The stale-token case itself (2026-11-01: Panzer Dragoon Orta's movie thread got the token of an
     *    older event, 0x48000012 / host 0x5B8). A host handle is closed behind the table's back, Windows hands its
     *    number to the next new object - and the title, still holding the old token, closes "its" handle. Before
     *    the fix the new object got the old token, so that close killed it. Now: the new object has its own token,
     *    the old token resolves to nothing, and closing it leaves the new object working. */
    {
        extern void *xbox_bridge_host_handle(uint32_t token);
        uint32_t ev_args[4] = { OUT_VA, 0, 1, 0 };
        uint32_t old_tok = 0, new_tok = 0, set_args[2] = { 0, 0 }, st_new = 0;
        HANDLE host_old = NULL;
        int k, reused = 0, bad = 0;

        for (k = 0; k < 64 && !reused; k++) {
            call(S_CREATE_EVENT, 4, ev_args);
            old_tok = *(uint32_t *)(mem + OUT_VA);
            host_old = (HANDLE)xbox_bridge_host_handle(old_tok);
            CloseHandle(host_old);                                   /* behind the table's back */
            call(S_CREATE_EVENT, 4, ev_args);
            new_tok = *(uint32_t *)(mem + OUT_VA);
            if ((HANDLE)xbox_bridge_host_handle(new_tok) == host_old) {
                reused = 1;
                if (new_tok == old_tok || xbox_bridge_host_handle(old_tok) != NULL)
                    bad = 1;
                { uint32_t c = old_tok; call(S_CLOSE, 1, &c); }       /* the title closes its old token */
                set_args[0] = new_tok;
                st_new = call(S_SET_EVENT, 2, set_args);
                if ((int32_t)st_new < 0)
                    bad = 1;
            } else {
                uint32_t c = old_tok; call(S_CLOSE, 1, &c);
            }
            { uint32_t c = new_tok; call(S_CLOSE, 1, &c); }
        }
        snprintf(d, sizeof d, "reused=%d old 0x%08X new 0x%08X host %p set(new) 0x%08X",
                 reused, old_tok, new_tok, host_old, st_new);
        if (!reused)
            printf("  (Windows did not reuse a handle number in 64 tries: stale-token check not exercised)\n");
        check(!bad, "stale token after a host-side close: new object separate, survives the old close", d);
    }

    /* 6. Concurrent handle creation (see race_check). */
    check(race_check(d, sizeof d), "handle tokens created on 8 threads at once never collide", d);

    /* 3. No call logging past 2^31 kernel calls (slow; opt-in). */
    if (argc > 1 && !strcmp(argv[1], "wrap")) {
        const char *logf = "kernel_regressions_wrap_stderr.txt";
        const uint64_t total = 2147483648ull + 200000ull;
        const uint32_t arg = 0x80001000u;
        uint32_t *sp = (uint32_t *)(mem + STACK_VA);
        recomp_func_t fn;
        uint64_t n;
        unsigned long lines = 0;
        FILE *f;
        char line[512];

        fflush(stderr);
        if (!freopen(logf, "w", stderr)) {
            puts("cannot redirect stderr");
            return 2;
        }
        setvbuf(stderr, NULL, _IOFBF, 1 << 20);

        fn = resolve(S_MMGPA);
        for (n = 0; n < total; n++) {
            sp[0] = 0xBEEF0001u;
            sp[1] = arg;
            g_esp = STACK_VA;
            RECOMP_CALL_GUEST(fn);
        }
        fflush(stderr);
        fclose(stderr);

        f = fopen(logf, "r");
        if (f) {
            while (fgets(line, sizeof line, f))
                if (strstr(line, "[KERNEL] #"))
                    lines++;
            fclose(f);
            remove(logf);
        }
        /* The first calls are logged up to the budget (default 200); anything
         * beyond that means the gate reopened after the wrap. */
        snprintf(d, sizeof d, "%lu call-log lines for %llu calls; at most 200 expected",
                 lines, (unsigned long long)total);
        check(f != NULL && lines <= 200,
              "no call logging after 2^31 kernel calls", d);
    } else {
        puts("  (2^31 call-counter check skipped; run with 'wrap' to include it)");
    }

    VirtualFree(mem, 0, MEM_RELEASE);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
