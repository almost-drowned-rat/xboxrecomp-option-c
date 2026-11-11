/*
 * host_stack_dump.c - where every host thread of the process is, for a hang report (Linux, Android).
 *
 * Windows has its own sampler (SuspendThread + GetThreadContext, kernel_bridge.c xbox_DpcHangReport). Here each
 * thread of /proc/self/task gets a real-time signal; its handler copies the interrupted pc, sp, fp and lr and holds
 * the thread still while this thread walks the frame records (process_vm_readv, between sp and sp + 8 MB) and
 * names the addresses with dladdr ("libORTA_game.so+0x1234"; symbolize with llvm-symbolizer /
 * addr2line against the same .so). Plus each thread's name, state and CPU ticks from /proc, so a spinning thread
 * stands out (state R, ticks climbing between two dumps).
 *
 * Only for diagnostics after a stall was seen: a thread blocked in a system call is interrupted once (SA_RESTART
 * restarts most calls; a sleep may end early).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE                     /* glibc: dladdr, REG_RIP; before the first system header */
#endif
#include "host_stack_dump.h"

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))

#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define HSD_FRAMES 16
#define HSD_STACK_SPAN (8u << 20)

/* The handler only copies four registers and then holds its thread still (bounded) while the dumping thread
 * walks the frames with process_vm_readv - a bad frame pointer is an EFAULT there, never a fault in a handler. */
static struct {
    volatile sig_atomic_t done, hold;
    uintptr_t pc, sp, fp, lr;
} s_slot;

static void hsd_handler(int sig, siginfo_t *si, void *ucv)
{
    const ucontext_t *uc = (const ucontext_t *)ucv;
    struct timespec ts = { 0, 1000000 };
    int i;
    (void)sig;
    (void)si;
#if defined(__aarch64__)
    s_slot.pc = (uintptr_t)uc->uc_mcontext.pc;
    s_slot.sp = (uintptr_t)uc->uc_mcontext.sp;
    s_slot.fp = (uintptr_t)uc->uc_mcontext.regs[29];
    s_slot.lr = (uintptr_t)uc->uc_mcontext.regs[30];
#else
    s_slot.pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    s_slot.sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    s_slot.fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
    s_slot.lr = 0;
#endif
    __atomic_store_n(&s_slot.done, 1, __ATOMIC_RELEASE);
    for (i = 0; i < 500 && __atomic_load_n(&s_slot.hold, __ATOMIC_ACQUIRE); i++)
        nanosleep(&ts, NULL);           /* async-signal-safe; at most 0.5 s */
}

static int read_self(uintptr_t at, void *to, size_t n)
{
    struct iovec l = { to, n }, r = { (void *)at, n };
    return syscall(SYS_process_vm_readv, (long)getpid(), &l, 1L, &r, 1L, 0L) == (long)n;
}

/* pc, lr, then the frame records: [fp] = the caller's fp, [fp + 8] = the return address */
static int walk(uintptr_t *pcs, int max)
{
    uintptr_t fp = s_slot.fp, sp = s_slot.sp;
    int n = 0;
    pcs[n++] = s_slot.pc;
    if (s_slot.lr)
        pcs[n++] = s_slot.lr & 0x0000FFFFFFFFFFFFull;   /* a leaf has not saved it yet; may be PAC-signed */
    while (n < max && fp >= sp && fp - sp < HSD_STACK_SPAN && !(fp & 7u)) {
        uintptr_t rec[2];
        if (!read_self(fp, rec, sizeof rec) || !rec[1])
            break;
#if defined(__aarch64__)
        rec[1] &= 0x0000FFFFFFFFFFFFull;    /* pointer authentication signs saved return addresses (bits 48+) */
#endif
        if (rec[1] != pcs[n - 1])
            pcs[n++] = rec[1];
        if (rec[0] <= fp)
            break;
        fp = rec[0];
    }
    return n;
}

static void name_addr(uintptr_t a, char *out, size_t n)
{
    Dl_info di;
    if (dladdr((void *)a, &di) && di.dli_fname) {
        const char *b = strrchr(di.dli_fname, '/');
        b = b ? b + 1 : di.dli_fname;
        if (di.dli_sname && di.dli_saddr)
            snprintf(out, n, "%s+0x%lx(%s+0x%lx)", b, (unsigned long)(a - (uintptr_t)di.dli_fbase), di.dli_sname,
                     (unsigned long)(a - (uintptr_t)di.dli_saddr));
        else
            snprintf(out, n, "%s+0x%lx", b, (unsigned long)(a - (uintptr_t)di.dli_fbase));
    } else {
        snprintf(out, n, "0x%lx", (unsigned long)a);
    }
}

/* name, state and utime + stime of one thread ("?" when /proc says nothing) */
static void thread_info(int tid, char *name, size_t nn, char *state, unsigned long long *ticks)
{
    char p[96], buf[512], *rp;
    FILE *f;
    snprintf(name, nn, "?");
    *state = '?';
    *ticks = 0;
    snprintf(p, sizeof p, "/proc/self/task/%d/comm", tid);
    if ((f = fopen(p, "r")) != NULL) {
        if (fgets(name, (int)nn, f))
            name[strcspn(name, "\n")] = 0;
        fclose(f);
    }
    snprintf(p, sizeof p, "/proc/self/task/%d/stat", tid);
    if ((f = fopen(p, "r")) != NULL) {
        if (fgets(buf, sizeof buf, f) && (rp = strrchr(buf, ')')) != NULL) {
            unsigned long long ut = 0, st = 0;
            /* after ") ": state(3) ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime(14) stime */
            if (sscanf(rp + 2, "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", state, &ut, &st) == 3)
                *ticks = ut + st;
        }
        fclose(f);
    }
}

int xbox_HostStackDump(FILE *out, const char *why)
{
    struct sigaction sa, old;
    int sig = SIGRTMAX - 3, self = (int)syscall(SYS_gettid), pid = (int)getpid(), threads = 0;
    DIR *d;
    struct dirent *e;

    if (!out)
        out = stderr;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = hsd_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, &old) != 0)
        return 0;
    fprintf(out, "  [STACKS] %s - every thread (symbolize offsets against the named file):\n", why ? why : "dump");
    if ((d = opendir("/proc/self/task")) != NULL) {
        while ((e = readdir(d)) != NULL) {
            int tid = atoi(e->d_name), i, waited;
            char name[64], state, line[2048];
            unsigned long long ticks;
            size_t at;
            if (tid <= 0)
                continue;
            thread_info(tid, name, sizeof name, &state, &ticks);
            at = (size_t)snprintf(line, sizeof line, "  [STACKS] tid %d \"%s\" %c cpu %llu:", tid, name, state, ticks);
            if (tid == self) {
                snprintf(line + at, sizeof line - at, " (the dumping thread)");
            } else {
                uintptr_t pcs[HSD_FRAMES];
                int n = 0;
                __atomic_store_n(&s_slot.done, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&s_slot.hold, 1, __ATOMIC_RELEASE);
                if (syscall(SYS_tgkill, pid, tid, sig) == 0) {
                    struct timespec ts = { 0, 1000000 };
                    for (waited = 0; !__atomic_load_n(&s_slot.done, __ATOMIC_ACQUIRE) && waited < 100; waited++)
                        nanosleep(&ts, NULL);
                }
                if (__atomic_load_n(&s_slot.done, __ATOMIC_ACQUIRE))
                    n = walk(pcs, HSD_FRAMES);
                __atomic_store_n(&s_slot.hold, 0, __ATOMIC_RELEASE);
                if (!n) {
                    snprintf(line + at, sizeof line - at, " (no answer)");
                } else {
                    for (i = 0; i < n && at + 1 < sizeof line; i++) {
                        char a[256];
                        name_addr(pcs[i], a, sizeof a);
                        at += (size_t)snprintf(line + at, sizeof line - at, " %s", a);
                    }
                }
                /* a late answer must not land in the next thread's slot: wait until this handler has left */
                for (waited = 0; waited < 20; waited++) {
                    struct timespec ts = { 0, 1000000 };
                    nanosleep(&ts, NULL);
                }
            }
            fprintf(out, "%s\n", line);
            threads++;
        }
        closedir(d);
    }
    fflush(out);
    sigaction(sig, &old, NULL);
    return threads;
}

#else

int xbox_HostStackDump(FILE *out, const char *why)
{
    (void)out;
    (void)why;
    return 0;
}

#endif
