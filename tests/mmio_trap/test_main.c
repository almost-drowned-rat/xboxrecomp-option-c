/*
 * mmio_trap: a register page that faults, serviced by a vectored exception handler through the shared decoder
 * (platform/mmio_decode.h) - the way the pads' USB host controller, the APU and the AC97 trap work. On Windows
 * that is the system's VEH; on x86-64 Linux the compat layer's signal handler (win32_compat.c), which was a stub
 * returning NULL before (no pad and no APU registers on Linux). The same checks run on both:
 *   1. reads of 8 / 16 / 32 bits come back from the device model, writes reach it, with the right width;
 *   2. ExceptionInformation[0] says write for a store, read for a load;
 *   3. a read-modify-write and a flag test on a register (the poll-loop forms) work;
 *   4. FirstHandler = 1 puts a handler in front of the ones registered earlier;
 *   5. single step: a handler opens the page and sets TF, the single-step exception closes it again (AC97 trap;
 *      x86 only - arm64 has no trap flag);
 *   6. Linux: a fault nobody claims still ends the process with SIGSEGV (chained to the default action).
 * arm64 Linux / Android (2026-11-11): the same handler with the arm64 CONTEXT, the load/store decoded by
 * mmio_decode_a64.h; the write flag comes from the ESR record of the signal frame. Checks 1-4 and 6.
 */
#include "mmio_decode.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include "win32_compat.h"
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define TEST_SINGLE_STEP 1
#else
#define TEST_SINGLE_STEP 0
#endif

#if !defined(MMIO_DECODE_AVAILABLE)
int main(void) { puts("mmio_trap: no MMIO trap on this host (decoders: x86-64, arm64); skipped"); return 0; }
#else

static int failures;
static void check(int ok, const char *what)
{
    printf("  %-72s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static uint8_t *s_page;                 /* the trapped "register" page */
/* volatile: the handler changes them behind main's back (the compiler cannot see that a load faults). */
static volatile uint32_t s_regs[1024];  /* the device model behind it */
static volatile int s_reads, s_writes, s_last_write_flag, s_last_size;
static volatile int s_order[4], s_order_n;

static uint64_t dev_rd(void *dev, uint32_t off, int size)
{
    uint64_t v = 0;
    (void)dev;
    s_reads++;
    s_last_size = size;
    memcpy(&v, (uint8_t *)(uintptr_t)s_regs + off, (size_t)size);
    return v;
}

static void dev_wr(void *dev, uint32_t off, uint64_t val, int size)
{
    (void)dev;
    s_writes++;
    s_last_size = size;
    memcpy((uint8_t *)(uintptr_t)s_regs + off, &val, (size_t)size);
}

static LONG CALLBACK first_handler(PEXCEPTION_POINTERS ep)
{
    (void)ep;
    if (s_order_n < 4)
        s_order[s_order_n++] = 1;
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK mmio_handler(PEXCEPTION_POINTERS ep)
{
    uintptr_t a;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    a = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
    if (a < (uintptr_t)s_page || a >= (uintptr_t)s_page + 4096)
        return EXCEPTION_CONTINUE_SEARCH;
    if (s_order_n < 4)
        s_order[s_order_n++] = 2;
    s_last_write_flag = (int)ep->ExceptionRecord->ExceptionInformation[0];
    return mmio_emulate(ep->ContextRecord, (uint32_t)(a - (uintptr_t)s_page), NULL, dev_rd, dev_wr)
           ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

#if TEST_SINGLE_STEP
/* 5: a second page, opened for one instruction (TF) and closed on the single step. */
static uint8_t *s_step_page;
static volatile int s_step_faults, s_steps;
static LONG CALLBACK step_handler(PEXCEPTION_POINTERS ep)
{
    DWORD old;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP && s_step_faults > s_steps) {
        s_steps++;
        VirtualProtect(s_step_page, 4096, PAGE_READONLY, &old);
        ep->ContextRecord->EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        if (a >= (uintptr_t)s_step_page && a < (uintptr_t)s_step_page + 4096) {
            s_step_faults++;
            VirtualProtect(s_step_page, 4096, PAGE_READWRITE, &old);
            ep->ContextRecord->EFlags |= 0x100u;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(void)
{
    DWORD old;
    volatile uint32_t *r32;
    volatile uint16_t *r16;
    volatile uint8_t *r8;
    uint32_t v;

    setvbuf(stdout, NULL, _IONBF, 0);
    s_page = (uint8_t *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!s_page || !AddVectoredExceptionHandler(0, mmio_handler)
            || !VirtualProtect(s_page, 4096, PAGE_NOACCESS, &old)) {
        puts("could not set up the trapped page / handler");
        return 2;
    }
    r32 = (volatile uint32_t *)(s_page + 0x100);
    r16 = (volatile uint16_t *)(s_page + 0x204);
    r8  = (volatile uint8_t *)(s_page + 0x30A);

    /* 1 + 2 */
    s_regs[0x100 / 4] = 0x12345678u;
    v = *r32;
    check(v == 0x12345678u && s_reads == 1 && s_last_size == 4, "32-bit read comes from the device model");
    check(s_last_write_flag == 0, "a load is reported as a read");
    *r32 = 0xCAFEF00Du;
    check(s_regs[0x100 / 4] == 0xCAFEF00Du && s_writes == 1 && s_last_size == 4, "32-bit write reaches the device");
    check(s_last_write_flag == 1, "a store is reported as a write");
    s_regs[0x204 / 4] = 0xBEEFu;
    check(*r16 == 0xBEEFu && s_last_size == 2, "16-bit read");
    *r8 = 0x5A;
    check(((volatile uint8_t *)s_regs)[0x30A] == 0x5A && s_last_size == 1, "8-bit write");

    /* 3: read-modify-write and a flag test, as a poll loop compiles them */
    s_regs[0x100 / 4] = 0x10u;
    *r32 |= 0x3u;
    check(s_regs[0x100 / 4] == 0x13u, "read-modify-write (or [reg], imm) through the trap");
    s_regs[0x100 / 4] = 0x4u;
    check((*r32 & 0x4u) != 0, "flag test on a trapped register (bit set)");
    s_regs[0x100 / 4] = 0x0u;
    check((*r32 & 0x4u) == 0, "flag test on a trapped register (bit clear)");

    /* 4: FirstHandler = 1 runs before the earlier one */
    AddVectoredExceptionHandler(1, first_handler);
    s_order_n = 0;
    v = *r32;
    check(s_order_n >= 2 && s_order[0] == 1 && s_order[1] == 2, "FirstHandler = 1 puts the handler in front");

#if TEST_SINGLE_STEP
    /* 5: single step */
    s_step_page = (uint8_t *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    AddVectoredExceptionHandler(1, step_handler);
    VirtualProtect(s_step_page, 4096, PAGE_READONLY, &old);
    ((volatile uint32_t *)s_step_page)[3] = 7;
    ((volatile uint32_t *)s_step_page)[4] = 9;
    check(s_step_faults == 2 && s_steps == 2 && ((uint32_t *)s_step_page)[3] == 7
          && ((uint32_t *)s_step_page)[4] == 9, "open for one instruction, closed again on the single step");
#endif

#if !defined(_WIN32)
    /* 6: an unclaimed fault still kills the process with SIGSEGV */
    {
        pid_t pid = fork();
        if (pid == 0) {
            volatile uint32_t *bad = (volatile uint32_t *)(uintptr_t)16;
            v = *bad;
            _exit(0);
        } else {
            int st = 0;
            waitpid(pid, &st, 0);
            check(WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV, "an unclaimed fault ends the process with SIGSEGV");
        }
    }
#endif
    (void)v;
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
#endif
