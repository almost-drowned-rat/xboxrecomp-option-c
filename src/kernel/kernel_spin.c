/* kernel_spin.c - the backoff behind RECOMP_SPIN() (recomp_types.h): the back edge of a guest polling loop
 * (tools/recomp/spin_detect.py, lifted with --spin-backoff / [recomp] spin_backoff; step 6c, maintainer
 * 2026-11-17: a generic spin-wait replacement).
 *
 * One wait = consecutive calls on one thread less than SPIN_GAP_NS apart. Within a wait:
 *   the first SPIN_PAUSES rounds  the host's pause hint only (a wait that ends at once costs nothing extra),
 *   up to SPIN_YIELD_NS           a yield per round (the thread waited for may share the core),
 *   after that                    SPIN_SLEEP_NS sleeps (host_sleep_ns, no spin) - a wait for another host thread
 *                                 or the GPU no longer burns a core; it ends at most one sleep late.
 * Thread-local state only, no lock, no I/O (a hot path: it runs inside guest code, DPCs and ISRs too).
 * RECOMP_SPIN_BACKOFF=0 keeps the pause hint only (for an A/B without a re-lift). */
#include "platform/host_wait.h"
#include "platform/xbox_winnt.h"

#include <stdint.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define SPIN_TLS __declspec(thread)
#else
#define SPIN_TLS __thread
#endif

#define SPIN_GAP_NS    200000ull     /* 0.2 ms without a call: the next one starts a new wait */
#define SPIN_PAUSES    64u
#define SPIN_YIELD_NS  1000000ull    /* 1 ms of yields, then sleeps */
#define SPIN_SLEEP_NS  100000ull     /* 0.1 ms */

static SPIN_TLS uint64_t t_last, t_start;
static SPIN_TLS uint32_t t_rounds;
static int s_mode = -1;              /* RECOMP_SPIN_BACKOFF: 1 backoff (default), 0 pause hint only */

static void spin_hint(void)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    _mm_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
    __yield();
#elif defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

void xbox_spin_backoff(void)
{
    uint64_t now;
    if (s_mode < 0) {
        const char *e = getenv("RECOMP_SPIN_BACKOFF");
        s_mode = !(e && *e == '0');
    }
    if (!s_mode) {
        spin_hint();
        return;
    }
    now = host_time_ns();
    if (now - t_last > SPIN_GAP_NS) {
        t_start = now;
        t_rounds = 0;
    }
    t_last = now;
    if (++t_rounds <= SPIN_PAUSES)
        spin_hint();
    else if (now - t_start < SPIN_YIELD_NS)
        SwitchToThread();
    else {
        host_sleep_ns(SPIN_SLEEP_NS);
        t_last = host_time_ns();         /* the sleep itself does not end the wait */
    }
}
