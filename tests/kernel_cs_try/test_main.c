/*
 * RtlTryEnterCriticalSection (306) with RtlLeaveCriticalSection (294) and RtlEnterCriticalSection (277), through the
 * thunk dispatcher, on two host threads (2026-11-01).
 *
 * The bridge kept a private host lock for TryEnter while Leave released the shared one, so after a Try + Leave the
 * try lock stayed held for ever and the shared one was "released" by a thread that did not own it. Panzer Dragoon
 * Orta's CRI file layer (TryEnter/Leave on 0x0042E668 every tick) stopped at ~41 s. Checked:
 *   1. thread A: TryEnter succeeds; thread B: TryEnter fails while A holds it;
 *   2. A leaves; B: TryEnter now succeeds (failed before the fix: the try lock was never released), B leaves;
 *   3. A: TryEnter + Leave, then B: a plain Enter does not block (and Leave).
 * Synthetic memory as tests/kernel_regressions; no title, no game files.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"

#include <stdio.h>
#include <stdlib.h>

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);
extern ptrdiff_t g_xbox_mem_offset;

#define THUNK_VA 0x10000u
#define CS_VA    0x30000u
enum { S_INIT, S_TRY, S_LEAVE, S_ENTER, N_SLOTS };
static const uint32_t ORD[N_SLOTS] = { 291, 306, 294, 277 };

static uint8_t *mem;
static uint32_t slot_va[N_SLOTS];
static int failures;

static void check(int ok, const char *what)
{
    printf("  %-64s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

/* One stdcall kernel call with one argument on a given guest register set and stack. */
static uint32_t call_on(X86Ctx *ctx, uint32_t stack_va, int slot, uint32_t arg)
{
    uint32_t *sp = (uint32_t *)(mem + stack_va);
    sp[0] = 0xBEEF0001u;
    sp[1] = arg;
    g_esp = stack_va;
    g_eax = 0xDEADBEEFu;
    RECOMP_CALL_GUEST(recomp_lookup_kernel(slot_va[slot]));
    return g_eax;
}

static X86Ctx s_a = X86CTX_INIT, s_b = X86CTX_INIT;
#define STACK_A 0x20000u
#define STACK_B 0x21000u

/* Thread B's steps, run on its own host thread and waited for. */
typedef struct { int op; uint32_t result; } Step;
static DWORD WINAPI thread_b(LPVOID p)
{
    Step *s = (Step *)p;
    if (s->op == S_ENTER) {
        call_on(&s_b, STACK_B, S_ENTER, CS_VA);
        s->result = 1;
        call_on(&s_b, STACK_B, S_LEAVE, CS_VA);
    } else {
        s->result = call_on(&s_b, STACK_B, S_TRY, CS_VA) & 0xFF;
        if (s->result)
            call_on(&s_b, STACK_B, S_LEAVE, CS_VA);
    }
    return 0;
}

static uint32_t on_b(int op, DWORD timeout_ms, int *timed_out)
{
    Step s = { op, 0xFFFFFFFFu };
    HANDLE t = CreateThread(NULL, 0, thread_b, &s, 0, NULL);
    DWORD w = WaitForSingleObject(t, timeout_ms);
    *timed_out = w != WAIT_OBJECT_0;
    if (!*timed_out)
        CloseHandle(t);
    return s.result;
}

int main(void)
{
    int to, i;
    uint32_t r;

    setvbuf(stdout, NULL, _IONBF, 0);
    mem = VirtualAlloc(NULL, 16 * 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!mem) { puts("no memory"); return 2; }
    g_xbox_mem_offset = (ptrdiff_t)mem;
    for (i = 0; i < N_SLOTS; i++)
        *(uint32_t *)(mem + THUNK_VA + 4 * i) = 0x80000000u | ORD[i];
    xbox_kernel_set_thunk_address(THUNK_VA, N_SLOTS);
    xbox_kernel_bridge_init();
    for (i = 0; i < N_SLOTS; i++) {
        slot_va[i] = *(uint32_t *)(mem + THUNK_VA + 4 * i);
        if (!recomp_lookup_kernel(slot_va[i])) { printf("ordinal %u did not resolve\n", ORD[i]); return 2; }
    }
    call_on(&s_a, STACK_A, S_INIT, CS_VA);

    /* 1 */
    check((call_on(&s_a, STACK_A, S_TRY, CS_VA) & 0xFF) == 1, "A: TryEnter on a free section succeeds");
    r = on_b(S_TRY, 5000, &to);
    check(!to && r == 0, "B: TryEnter fails while A holds it");

    /* 2 */
    call_on(&s_a, STACK_A, S_LEAVE, CS_VA);
    r = on_b(S_TRY, 5000, &to);
    check(!to && r == 1, "A left: B's TryEnter succeeds (the try lock was released)");

    /* 3 */
    check((call_on(&s_a, STACK_A, S_TRY, CS_VA) & 0xFF) == 1, "A: TryEnter again");
    call_on(&s_a, STACK_A, S_LEAVE, CS_VA);
    r = on_b(S_ENTER, 5000, &to);
    check(!to && r == 1, "after A's TryEnter + Leave, B's plain Enter does not block");

    printf("\n%d failure(s)\n", failures);
    fflush(stdout);
    ExitProcess(failures ? 1 : 0);
}
