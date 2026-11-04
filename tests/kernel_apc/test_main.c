/*
 * File I/O completion APCs: inline (default) and deferred (RECOMP_APC_DEFERRED=1).
 *
 * A title's ReadFileEx / WriteFileEx reaches the kernel as NtReadFile / NtWriteFile with an ApcRoutine. On the
 * console the routine runs at the issuing thread's next alertable user-mode wait, and that wait returns
 * STATUS_USER_APC (0xC0) instead of waiting. The bridge does the I/O synchronously and by default runs the routine
 * before NtWriteFile returns; Panzer Dragoon Orta clears its done flag after the call returns and then hung in
 * SleepEx(1, TRUE). With the switch the routine is queued and run at that wait.
 *
 * Like tests/kernel_regressions this calls the bridges through the thunk dispatcher with a buffer standing in for
 * guest memory; the APC routine is a host function the stand-in recomp_lookup hands out. The switch is read once
 * per process, so CMake runs the program twice: without an argument (inline) and with "deferred".
 */
#include "kernel.h"
#include "xbox_memory_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86Ctx s_test_ctx = X86CTX_INIT;
#define ctx (&s_test_ctx)

#define THUNK_VA  0x10000u
#define STACK_VA  0x20000u     /* the stack grows down from here */
#define IOSB_VA   0x30000u
#define BUF_VA    0x30100u
#define APC_VA    0x00401000u  /* the "guest" completion routine */
#define APC_CTX   0x1234ABCDu

static int s_apc_runs;
static uint32_t s_apc_ctx_seen;

/* VOID NTAPI ApcRoutine(PVOID ApcContext, PIO_STATUS_BLOCK, ULONG): note the call; stdcall, pops its 12 bytes. */
static void test_apc(X86Ctx *c, uint8_t *b)
{
    (void)c; (void)b;
    s_apc_ctx_seen = *(uint32_t *)((uint8_t *)g_xbox_mem_offset + g_esp + 4);
    s_apc_runs++;
    g_esp += 16;
}

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { return xbox_va == APC_VA ? test_apc : NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);
extern ptrdiff_t g_xbox_mem_offset;

enum { S_WRITE, S_DELAY, S_WAITEX, N_SLOTS };
static const uint32_t ORD[N_SLOTS] = {
    236,   /* NtWriteFile */
    99,    /* KeDelayExecutionThread */
    234,   /* NtWaitForSingleObjectEx */
};

static uint8_t *mem;
static uint32_t slot_va[N_SLOTS];
static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-66s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

static uint32_t call(int slot, int nargs, const uint32_t *args)
{
    uint32_t *sp;
    int i;

    g_esp = STACK_VA - 0x100u;
    sp = (uint32_t *)(mem + g_esp);
    sp[0] = 0xBEEF0001u;
    for (i = 0; i < nargs; i++)
        sp[1 + i] = args[i];
    g_eax = 0xDEADBEEFu;
    RECOMP_CALL_GUEST(recomp_lookup_kernel(slot_va[slot]));
    return g_eax;
}

/* WriteFileEx's kernel call: 4 bytes to the temp file, completion routine APC_VA. */
static uint32_t write_ex(uint32_t token)
{
    uint32_t a[8] = { token, 0, APC_VA, APC_CTX, IOSB_VA, BUF_VA, 4, 0 };
    memcpy(mem + BUF_VA, "APC!", 4);
    return call(S_WRITE, 8, a);
}

/* SleepEx(1, alertable): KeDelayExecutionThread(UserMode, alertable, -10000 = 1 ms relative). */
static uint32_t sleep_ex(uint32_t mode, uint32_t alertable)
{
    uint32_t a[3] = { mode, alertable, IOSB_VA + 0x40u };
    *(int64_t *)(mem + IOSB_VA + 0x40u) = -10000;
    return call(S_DELAY, 3, a);
}

int main(int argc, char **argv)
{
    char d[160], path[MAX_PATH], dir[MAX_PATH];
    int deferred = argc > 1 && !strcmp(argv[1], "deferred");
    HANDLE f;
    uint32_t token, st;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    _putenv(deferred ? "RECOMP_APC_DEFERRED=1" : "RECOMP_APC_DEFERRED=");
    printf("completion APCs %s\n", deferred ? "deferred (RECOMP_APC_DEFERRED=1)" : "inline (default)");

    mem = VirtualAlloc(NULL, 16 * 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!mem) { puts("could not allocate the guest memory stand-in"); return 2; }
    g_xbox_mem_offset = (ptrdiff_t)mem;

    for (i = 0; i < N_SLOTS; i++)
        *(uint32_t *)(mem + THUNK_VA + 4 * i) = 0x80000000u | ORD[i];
    xbox_kernel_set_thunk_address(THUNK_VA, N_SLOTS);
    xbox_kernel_bridge_init();
    for (i = 0; i < N_SLOTS; i++) {
        slot_va[i] = *(uint32_t *)(mem + THUNK_VA + 4 * i);
        if (!recomp_lookup_kernel(slot_va[i])) { printf("ordinal %u did not resolve\n", ORD[i]); return 2; }
    }

    GetTempPathA(sizeof dir, dir);
    GetTempFileNameA(dir, "apc", 0, path);
    f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (f == INVALID_HANDLE_VALUE || (uintptr_t)f > 0xFFFFFFF0u) { puts("cannot create the temp file"); return 2; }
    token = (uint32_t)(uintptr_t)f;      /* an untagged token is passed through as the host handle */

    st = write_ex(token);
    snprintf(d, sizeof d, "status 0x%08X, IOSB status 0x%08X info %u", st,
             *(uint32_t *)(mem + IOSB_VA), *(uint32_t *)(mem + IOSB_VA + 4));
    check(*(uint32_t *)(mem + IOSB_VA) == 0 && *(uint32_t *)(mem + IOSB_VA + 4) == 4, "NtWriteFile wrote 4 bytes", d);

    if (!deferred) {
        snprintf(d, sizeof d, "%d runs", s_apc_runs);
        check(s_apc_runs == 1 && s_apc_ctx_seen == APC_CTX, "inline: the APC ran before NtWriteFile returned", d);
        st = sleep_ex(1, 1);
        snprintf(d, sizeof d, "returned 0x%08X, %d runs", st, s_apc_runs);
        check(st != 0xC0u && s_apc_runs == 1, "inline: the alertable wait after it waits normally", d);
    } else {
        snprintf(d, sizeof d, "%d runs", s_apc_runs);
        check(s_apc_runs == 0, "deferred: NtWriteFile returned with the APC still queued", d);

        st = sleep_ex(1, 0);
        snprintf(d, sizeof d, "returned 0x%08X, %d runs", st, s_apc_runs);
        check(st != 0xC0u && s_apc_runs == 0, "deferred: a non-alertable wait does not run it", d);

        st = sleep_ex(0, 1);
        snprintf(d, sizeof d, "returned 0x%08X, %d runs", st, s_apc_runs);
        check(st != 0xC0u && s_apc_runs == 0, "deferred: an alertable kernel-mode wait does not run it", d);

        g_ebx = 0x11111111u; g_esi = 0x22222222u; g_edi = 0x33333333u;
        st = sleep_ex(1, 1);
        snprintf(d, sizeof d, "returned 0x%08X, %d runs, context 0x%08X", st, s_apc_runs, s_apc_ctx_seen);
        check(st == 0xC0u && s_apc_runs == 1 && s_apc_ctx_seen == APC_CTX,
              "deferred: SleepEx(alertable) runs it, returns STATUS_USER_APC", d);
        check(g_ebx == 0x11111111u && g_esi == 0x22222222u && g_edi == 0x33333333u,
              "deferred: the caller's registers survive the APC", NULL);

        st = sleep_ex(1, 1);
        snprintf(d, sizeof d, "returned 0x%08X, %d runs", st, s_apc_runs);
        check(st != 0xC0u && s_apc_runs == 1, "deferred: the next alertable wait waits normally", d);

        /* Two requests, then WaitForSingleObjectEx(alertable) on the file handle: both run, in order. */
        write_ex(token);
        write_ex(token);
        {
            uint32_t a[4] = { token, 1, 1, 0 };
            st = call(S_WAITEX, 4, a);
        }
        snprintf(d, sizeof d, "returned 0x%08X, %d runs", st, s_apc_runs);
        check(st == 0xC0u && s_apc_runs == 3, "deferred: NtWaitForSingleObjectEx runs both queued APCs", d);
    }

    CloseHandle(f);
    VirtualFree(mem, 0, MEM_RELEASE);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
