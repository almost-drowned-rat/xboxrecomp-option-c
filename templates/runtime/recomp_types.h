/**
 * Xbox Static Recompilation - Runtime Type Definitions
 *
 * Type definitions and helper macros used by mechanically translated
 * x86 -> C code. Each original x86 function is translated to a C
 * function that uses these types and macros.
 *
 * This is a reusable template for ANY Xbox game. Game-specific
 * customization should go in separate headers.
 *
 * Memory model:
 *   Xbox data sections are mapped to their original VAs via
 *   CreateFileMapping + MapViewOfFileEx (see xbox_memory.h).
 *   Recompiled code accesses globals via pointer casts, e.g.:
 *     *(uint32_t*)0x003B2360
 *
 * Register model:
 *   Volatile registers (eax, ecx, edx, esp) are global variables,
 *   matching real x86 behavior where these registers are shared
 *   across all code. This enables correct argument passing via the
 *   simulated stack and return value communication via eax.
 *
 *   Callee-saved registers (ebx, esi, edi) are also global because
 *   callers pass implicit parameters through them (e.g. 'this' via
 *   esi in thiscall). The callee-save contract is enforced by
 *   PUSH32/POP32 instructions in the generated code, not by C local
 *   variable scoping.
 *
 *   ebp is NOT global - it stays local in each function because many
 *   FPO (Frame Pointer Omission) functions use it as scratch without
 *   save/restore. For SEH functions, g_seh_ebp bridges the gap.
 *
 * Calling convention:
 *   All translated functions are void(void). Arguments are passed
 *   on the simulated Xbox stack (via push instructions before call).
 *   Return values are communicated through g_eax.
 *   The call instruction pushes the real guest return address (the VA of
 *   the instruction after the call); ret discards it with esp += 4.
 *   The value is never used to transfer control -- control flow is C
 *   call/return -- but it must be correct because guest code reads it
 *   (__SEH_prolog's scope table, _alloca probes, "mov eax, [esp]").
 */

#ifndef RECOMP_TYPES_H
#define RECOMP_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* math.h is load-bearing, and its absence was invisible.
 *
 * The lifter emits sqrt() and fabs() for fsqrt/fabs -- 146 of them in one of
 * Halo's nine chunks alone -- and now sin/cos/tan/atan2/log2/exp2/fmod for the
 * x87 transcendentals. With no declaration in scope, C89 implicit declaration
 * makes every one of them return `int`: the caller reads EAX instead of XMM0 and
 * gets garbage, then converts that garbage to double. Vector normalisation is
 * 1/sqrt(x), so this corrupts every matrix the title builds.
 *
 * Nothing reported it because generated code is compiled with /w (see the game
 * CMakeLists) -- MSVC's C4013 was emitted and discarded. Same failure as the
 * missing stdlib.h in kernel_bridge.c, in a hotter path. */
#include <math.h>
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

/* MSVC's __forceinline -> gcc/clang equivalent on POSIX. */
#if !defined(_MSC_VER) && !defined(__forceinline)
#define __forceinline inline __attribute__((always_inline))
#endif

/* Guest debug output: INT 2D, the Xbox kernel debug trap.
 *
 * eax selects the service, ecx carries its argument. Service 1 prints the
 * ANSI_STRING ecx points at -- what OutputDebugStringA compiles to. The int3
 * that always follows is the slide byte the kernel steps over, so the lifter
 * emits nothing for it; lifting it to __debugbreak() terminated the process
 * with STATUS_BREAKPOINT the first time a title tried to print. */
void recomp_debug_service(uint32_t service, uint32_t arg_va);

/* MSVC's __debugbreak() intrinsic -> gcc/clang equivalent.
 * The auto-generated code emits __debugbreak for x86 INT 3 instructions.
 * !defined(__debugbreak) is not enough on its own: MinGW declares __debugbreak
 * as a function, not a macro, so that test passes and the macro is defined
 * anyway. Any windows.h reaching this TU afterwards then fails to declare it.
 * Excluding _WIN32 outright leaves the SDK's own int3 intrinsic in place. */
#if !defined(_MSC_VER) && !defined(_WIN32) && !defined(__debugbreak)
#define __debugbreak() __builtin_trap()
#endif

/* ================================================================
 * Memory offset
 * ================================================================ */

/**
 * Memory offset from Xbox VA to actual mapped address.
 * When Xbox memory is mapped at the original address (0x00010000),
 * this is 0 and the MEM macros are simple identity casts.
 * When mapped elsewhere, this adjusts all memory accesses.
 *
 * Set once during memory initialization, then read-only.
 */
extern ptrdiff_t g_xbox_mem_offset;

/* Bounds of the title's executable sections, filled in by
 * xbox_MemoryLayoutInit from the XBE's own section table. Zero means "not
 * loaded yet", which RECOMP_ICALL_IS_CODE treats as allow.
 *
 * These replace a hardcoded 0x00400000 cutoff that was only ever right for one
 * title -- see the comment where they are defined in xbox_memory_layout.c. */
/* --force-return: read once at startup, so the check at each forced ret is a
 * load rather than a getenv. Zero unless RECOMP_FORCE_RETURN is set, which is
 * what lets a build carrying forced functions behave normally by default. */
extern int g_force_return;

extern uint32_t g_xbox_code_lo;
extern uint32_t g_xbox_code_hi;

/* Is this indirect-call target plausibly code?
 *
 * Synthetic kernel thunks live at 0xFE0000xx and are dispatched by
 * recomp_lookup_kernel, so they are always allowed. Everything else has to lie
 * inside an executable section of the loaded title. A target that fails this is
 * a garbage pointer that reached a call site, and calling it would be worse
 * than dropping it. */
#define RECOMP_ICALL_IS_CODE(_va) \
    ((_va) >= 0xFE000000u || g_xbox_code_hi == 0u || \
     ((_va) >= g_xbox_code_lo && (_va) < g_xbox_code_hi))

/* ================================================================
 * Global registers
 * ================================================================ */

/**
 * Volatile x86 registers (caller-saved):
 *   eax - return values, general accumulator
 *   ecx - 'this' pointer for thiscall, loop counter
 *   edx - high dword of multiply/divide, general
 *   esp - stack pointer (initialized to top of Xbox stack)
 *
 * Callee-saved x86 registers (also global):
 *   ebx, esi, edi - global because callers pass implicit parameters
 *   through them. The callee-save contract is enforced by generated
 *   PUSH32/POP32 instructions.
 *
 * NOT global: ebp - stays local in each function because FPO
 * functions use it as scratch. For SEH, g_seh_ebp bridges the gap.
 */
/* Per-thread register state.
 *
 * These started as plain globals, which works exactly as long as one thread
 * runs recompiled code. Halo is the first title to create real workers (its
 * cache/file loader), and the runtime papered over that by running every worker
 * synchronously inside PsCreateSystemThreadEx -- so a worker that blocks
 * waiting for work never returns and startup deadlocks.
 *
 * On hardware each thread has its own register set, so model it that way.
 * Thread-local costs an indirection per access; a deadlock costs the title. */
#if defined(_MSC_VER)
#  define RECOMP_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define RECOMP_TLS __thread
#else
#  define RECOMP_TLS _Thread_local
#endif

/* --weak-functions: sub_X is a weak alias of the generated body sub_X_gen, so
 * a strong sub_X in the project (recomp_manual.c) replaces it at link time --
 * no re-lift, no extra call. GCC/clang on ELF and COFF (llvm-mingw); MSVC has
 * no weak alias, so there sub_X is a plain forwarder. */
#if defined(__GNUC__) || defined(__clang__)
#  define RECOMP_WEAK_ALIAS(name, target) \
       extern void name(RECOMP_PARAMS) __attribute__((weak, alias(#target)));
#else
#  define RECOMP_WEAK_ALIAS(name, target) \
       void name(RECOMP_PARAMS) { target(RECOMP_ARGS); }
#endif

/* SSE and MMX register storage (lane views over one register); the
 * register set below holds them. */
#ifndef RECOMP_XMM_DEFINED
#define RECOMP_XMM_DEFINED
typedef union RecompXmm {
    float    f[4];
    double   d[2];
    uint32_t u[4];
    int32_t  i[4];
    uint64_t q[2];
} RecompXmm;
#endif
#ifndef RECOMP_MMX_DEFINED
#define RECOMP_MMX_DEFINED
typedef union RecompMmx {
    int8_t   b[8];
    uint8_t  ub[8];
    int16_t  w[4];
    uint16_t uw[4];
    int32_t  d[2];
    uint32_t ud[2];
    uint64_t q;
} RecompMmx;
#endif

/* The guest register set (option C, C.1). One struct per guest thread instead
 * of one thread-local variable per register. Defined in both this header and
 * the other of recomp_types.h / src/kernel/xbox_memory_layout.h -- a
 * translation unit can include both -- and the two copies must stay identical
 * (tools/recomp/test_x86ctx_layout.py checks that).
 *
 * Every guest function -- generated, manual override, kernel thunk, XDK HLE
 * entry -- takes the register set and the guest base as parameters
 * (RECOMP_PARAMS), and so does every runtime function that reads or writes
 * guest registers on a path guest code can reach. The names below (g_eax,
 * g_esp, g_fp_stack, g_xmm0 ...) name the fields of the ctx in scope: a
 * function that uses them has to have one. Each host thread that runs guest
 * code owns its register set (xbox_ctx_init); there is no thread-local or
 * global instance (option C, C.1-4). */
/* X86Ctx is 64-byte aligned (one cache line) wherever it lives -- static, a
 * host thread's stack, the main thread: the speed of hot guest code depends on
 * where the register set sits relative to 32-byte boundaries (bench F5: ~73 vs
 * ~100 ns per call, docs/option_c.md), so the alignment is fixed by the type, not
 * left to the stack's per-process offset. Static and stack instances honour it
 * (the compiler realigns the frame); a heap copy needs an aligned allocation.
 * Checked at compile time below and, in debug builds, when a thread registers its
 * set (xbox_diag_ctx_set). */
#ifndef RECOMP_CTX_ALIGN
#if defined(_MSC_VER) && !defined(__clang__)
#define RECOMP_CTX_ALIGN __declspec(align(64))
#define RECOMP_CTX_ALIGNOF(t) __alignof(t)
#else
#define RECOMP_CTX_ALIGN __attribute__((aligned(64)))
#define RECOMP_CTX_ALIGNOF(t) __alignof__(t)
#endif
#endif
#ifndef RECOMP_X86CTX_DEFINED
#define RECOMP_X86CTX_DEFINED
typedef struct RECOMP_CTX_ALIGN X86Ctx {
    /* r_ prefix: generated code has object-like macros named eax..edi, which
     * would rewrite a member of the same name inside g_eax's expansion. */
    uint32_t  r_eax, r_ecx, r_edx, r_ebx, r_esp, r_ebp, r_esi, r_edi;
    uint32_t  seh_ebp;          /* SEH frame pointer bridge (recomp_types.h) */
    uint32_t  fs_base;          /* linear base of fs: this thread's TIB */
    uint32_t  eflags_sys;       /* EFLAGS.AC/ID as popfd left them */
    int       df;               /* EFLAGS.DF */
    double    fp_stack[8];      /* x87 stack, double-backed */
    int       fp_top;
    int       fp_cmp;
    uint16_t  fp_control_word;
    uint16_t  fp_cc;
    uint32_t  mxcsr;            /* SSE control/status, reset 0x1F80 (sp00n c50c597,
                                 * Kitae Noh); fills the padding before xmm, so no
                                 * field moves */
    RecompXmm xmm[8];
    RecompMmx mm[8];
    uint32_t  call_flags;       /* EFLAGS a callee hands to a caller that branches on
                                 * them after the call (lifter CALL_FLAGS); last, so
                                 * no hot field moves */
} X86Ctx;
typedef char recomp_x86ctx_is_64_byte_aligned[RECOMP_CTX_ALIGNOF(X86Ctx) == 64 ? 1 : -1];

#define g_eax             (ctx->r_eax)
#define g_ecx             (ctx->r_ecx)
#define g_edx             (ctx->r_edx)
#define g_ebx             (ctx->r_ebx)
#define g_esp             (ctx->r_esp)
#define g_ebp             (ctx->r_ebp)
#define g_esi             (ctx->r_esi)
#define g_edi             (ctx->r_edi)
#define g_seh_ebp         (ctx->seh_ebp)
#define g_fs_base         (ctx->fs_base)
#define g_eflags_sys      (ctx->eflags_sys)
#define g_call_flags      (ctx->call_flags)
#define g_df              (ctx->df)
#define g_fp_stack        (ctx->fp_stack)
#define g_fp_top          (ctx->fp_top)
#define g_fp_cmp          (ctx->fp_cmp)
#define g_fp_control_word (ctx->fp_control_word)
#define g_fp_cc           (ctx->fp_cc)
#define g_mxcsr           (ctx->mxcsr)
#define g_xmm0            (ctx->xmm[0])
#define g_xmm1            (ctx->xmm[1])
#define g_xmm2            (ctx->xmm[2])
#define g_xmm3            (ctx->xmm[3])
#define g_xmm4            (ctx->xmm[4])
#define g_xmm5            (ctx->xmm[5])
#define g_xmm6            (ctx->xmm[6])
#define g_xmm7            (ctx->xmm[7])
#define g_mm0             (ctx->mm[0])
#define g_mm1             (ctx->mm[1])
#define g_mm2             (ctx->mm[2])
#define g_mm3             (ctx->mm[3])
#define g_mm4             (ctx->mm[4])
#define g_mm5             (ctx->mm[5])
#define g_mm6             (ctx->mm[6])
#define g_mm7             (ctx->mm[7])

/* The signature of every guest function. ctx is restrict (option C, C.1-5):
 * nothing reaches a running function's register set except through this
 * pointer, so the compiler may keep registers in host registers across calls
 * and stores (tools/recomp/test_ctx_lint.py guards the rule). base, the host
 * address of guest address 0, is never restrict: guest RAM is shared with
 * other threads (executor, audio) and mapped twice. */
#define RECOMP_PARAMS X86Ctx *__restrict ctx, uint8_t *base
#define RECOMP_ARGS   ctx, base
typedef void (*recomp_func_t)(RECOMP_PARAMS);

/* Host code calling guest code on the register set in scope. */
extern ptrdiff_t g_xbox_mem_offset;
#define RECOMP_GUEST_BASE      ((uint8_t *)g_xbox_mem_offset)
#define RECOMP_CALL_GUEST(fn)  ((fn)(ctx, RECOMP_GUEST_BASE))
#endif


/* x87 stack. Per-thread for the same reason the integer registers are:
 * arguments are passed in st(0)/st(1) across call boundaries. */

/**
 * SEH frame pointer bridge.
 *
 * __SEH_prolog sets up ebp for the caller, but since ebp is a local
 * variable in each function, the caller can't see the prolog's change.
 * The prolog writes g_seh_ebp, and the caller reads it after the call.
 * Similarly, __SEH_epilog reads g_seh_ebp at entry and writes it at exit.
 */
/* Linear base of the fs segment: where the fake TIB lives. Generated code
 * adds this to every fs-relative address, so page zero stays unmapped and a
 * null dereference faults instead of hitting the TIB. Must match
 * XBOX_FS_BASE in src/kernel/xbox_memory_layout.h.
 *
 * Per-thread, because a TIB is. fs:[0] is the SEH chain head and fs:[4]
 * reaches the CRT's per-thread data, so a single shared base makes every
 * guest thread the same thread as far as the CRT is concerned. */
#define XBOX_FS_BASE g_fs_base


/* ---- non-local jumps (setjmp / longjmp) --------------------------------
 *
 * A recompiled function is a real C function, so restoring the guest esp is
 * only half of a longjmp: the abandoned frames are still on the native stack,
 * and control returns into them once the resume point finishes. Pairing each
 * guest jmp_buf with a native one -- taken at the setjmp call site, the only
 * place a native setjmp is valid -- makes the guest jump a native jump, so the
 * frames actually unwind.
 *
 * recomp_setjmp_slot() returns the native buffer for a guest jmp_buf address,
 * allocating one on first use. recomp_guest_longjmp() restores the guest
 * registers the CRT's longjmp would and jumps; it returns 0, without jumping,
 * for a buffer that has no native counterpart, leaving the caller to fall back
 * to the guest's own longjmp.
 */
/* Atomic read-modify-write, for the lock-prefixed instructions.
 *
 * "lock xadd" and "lock cmpxchg" are what InterlockedIncrement and
 * InterlockedCompareExchange compile to, so they carry a title's reference
 * counts and its lock-free lists. They used to lift to a TODO comment, which
 * meant a refcount that never moved and a compare-and-swap that never swapped
 * -- harmless while every guest thread ran synchronously, and not once the
 * runtime began spawning real ones.
 *
 * Genuinely atomic, not merely correct in isolation: the guest's own threads
 * now run on real host threads, so a read-modify-write that races is exactly
 * the bug these instructions exist to prevent.
 */
#if defined(_MSC_VER)
#include <intrin.h>
#define RECOMP_ATOMIC_ADD32(p, v)     ((uint32_t)_InterlockedExchangeAdd((volatile long *)(p), (long)(v)))
#define RECOMP_ATOMIC_CAS32(p, cmp, val)     ((uint32_t)_InterlockedCompareExchange((volatile long *)(p),                                            (long)(val), (long)(cmp)))
#define RECOMP_ATOMIC_CAS_OK8(p, cmp, val) \
    (_InterlockedCompareExchange8((volatile char *)(p), (char)(val), (char)(cmp)) == (char)(cmp))
#define RECOMP_ATOMIC_CAS_OK16(p, cmp, val) \
    (_InterlockedCompareExchange16((volatile short *)(p), (short)(val), (short)(cmp)) == (short)(cmp))
#define RECOMP_ATOMIC_CAS_OK32(p, cmp, val) \
    (_InterlockedCompareExchange((volatile long *)(p), (long)(val), (long)(cmp)) == (long)(cmp))
#define RECOMP_ATOMIC_XCHG8(p, v)  ((uint8_t)_InterlockedExchange8((volatile char *)(p), (char)(v)))
#define RECOMP_ATOMIC_XCHG16(p, v) ((uint16_t)_InterlockedExchange16((volatile short *)(p), (short)(v)))
#define RECOMP_ATOMIC_XCHG32(p, v) ((uint32_t)_InterlockedExchange((volatile long *)(p), (long)(v)))
#else
#define RECOMP_ATOMIC_ADD32(p, v)     ((uint32_t)__sync_fetch_and_add((volatile uint32_t *)(p), (uint32_t)(v)))
#define RECOMP_ATOMIC_CAS32(p, cmp, val)     ((uint32_t)__sync_val_compare_and_swap((volatile uint32_t *)(p),                                            (uint32_t)(cmp), (uint32_t)(val)))
#define RECOMP_ATOMIC_CAS_OK8(p, cmp, val) \
    __sync_bool_compare_and_swap((volatile uint8_t *)(p), (uint8_t)(cmp), (uint8_t)(val))
#define RECOMP_ATOMIC_CAS_OK16(p, cmp, val) \
    __sync_bool_compare_and_swap((volatile uint16_t *)(p), (uint16_t)(cmp), (uint16_t)(val))
#define RECOMP_ATOMIC_CAS_OK32(p, cmp, val) \
    __sync_bool_compare_and_swap((volatile uint32_t *)(p), (uint32_t)(cmp), (uint32_t)(val))
#define RECOMP_ATOMIC_XCHG8(p, v)  __atomic_exchange_n((volatile uint8_t *)(p), (uint8_t)(v), __ATOMIC_SEQ_CST)
#define RECOMP_ATOMIC_XCHG16(p, v) __atomic_exchange_n((volatile uint16_t *)(p), (uint16_t)(v), __ATOMIC_SEQ_CST)
#define RECOMP_ATOMIC_XCHG32(p, v) __atomic_exchange_n((volatile uint32_t *)(p), (uint32_t)(v), __ATOMIC_SEQ_CST)
#endif
/* RECOMP_ATOMIC_CAS_OK<n>: store val if *p still holds cmp; 1 if it did. The
 * locked read-modify-writes (lock add/sub/and/or/xor/adc/sbb/inc/dec/not/neg)
 * run their plain lift on a copy inside a compare-and-swap loop.
 * RECOMP_ATOMIC_XCHG<n>: store v, return the old value, as one atomic exchange
 * with a full fence -- xchg with memory, locked with or without the prefix. */

/*
 * x86 fences (option C, F2). They used to lift to nothing ("coherent host
 * memory"), which holds on x86-64 but not on ARM64: there another core may see
 * a thread's stores out of order - the pushbuffer data after the PUT/kickoff
 * write that D3D fences with sfence. Each maps to the C11 fence with the same
 * guarantee: sfence orders stores (release), lfence orders loads (acquire),
 * mfence orders everything (seq_cst). On x86-64 the first two are compiler
 * barriers only (x86 stores and loads are already ordered so), mfence is mfence;
 * on ARM64 they become dmb. MSVC (the 32-bit conformance oracle only) gets a
 * compiler barrier plus a hardware fence for all three.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#define RECOMP_FENCE_STORE() do { _ReadWriteBarrier(); _mm_mfence(); _ReadWriteBarrier(); } while (0)
#define RECOMP_FENCE_LOAD()  RECOMP_FENCE_STORE()
#define RECOMP_FENCE_FULL()  RECOMP_FENCE_STORE()
#else
#define RECOMP_FENCE_STORE() __atomic_thread_fence(__ATOMIC_RELEASE)
#define RECOMP_FENCE_LOAD()  __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define RECOMP_FENCE_FULL()  __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

#include <setjmp.h>
jmp_buf *recomp_setjmp_slot(uint32_t buf_va);
int recomp_guest_longjmp(X86Ctx *ctx, uint32_t buf_va, uint32_t value);

/* EFLAGS.DF, and the signed step the string instructions take because of it.
 *
 * `cld`/`std` used to lift to a comment and every string instruction stepped
 * forwards regardless. That is right almost everywhere -- DF is 0 by ABI and
 * the compiler restores it -- but MSVC's strrchr is `std; repne scasb` from
 * the terminator backwards, so it scanned forwards off the end of the string
 * and returned a pointer into whatever followed. The Xbox Dashboard uses it to
 * split "y:\default.xip" into a mount path; with strrchr wrong the archive
 * registered itself under its own full filename, no resource in it could ever
 * be found by name, and the dashboard rebooted rather than showing a UI.
 * memmove's overlapping case is the same instruction and the same bug.
 */
#define RECOMP_DF_STEP(n) (g_df ? -(int32_t)(n) : (int32_t)(n))

/* The EFLAGS bits pushfd reports beside the arithmetic ones: bit 1 (always
 * set), IF (interrupts on), DF from g_df, and AC/ID as popfd last wrote them.
 * pushfd/popfd used to be untranslated no-ops, which also left esp 4 bytes
 * off per instruction -- Bink's MMX probe (pushfd/popfd/cpuid) returned
 * through a shifted stack. */
#define RECOMP_EFLAGS_BASE() \
    (0x00000202u | (g_df ? 0x00000400u : 0u) | g_eflags_sys)

/* CPUID leaf 1 EDX the lifted cpuid reports: the Xbox Pentium III's
 * 0x0383FBFF (MMX, FXSR, SSE, no SSE2) unless RECOMP_CPUID_EDX=<hex> says
 * otherwise (read once by xbox_kernel_init). */
extern uint32_t g_recomp_cpuid_edx1;
#define RECOMP_CPUID_EDX1 g_recomp_cpuid_edx1

/* x87 control and status. Thread-local for the same reason the x87 stack
   above is: one guest routine can lift to several C functions, so a compare
   and the FNSTSW that reads it can land in different bodies, and the control
   word has to survive a call. (g_fp_stack/g_fp_top are declared above.) */
/* x87 precision control (control word bits 8-9). The stack is double-backed,
 * which matches PC=53 and is close enough for PC=64, but PC=24 -- what the
 * Xbox runs with -- rounds every arithmetic result to float. A title that
 * compares a freshly computed value against the same value stored as a float
 * relies on that: Burnout 3's sorted draw list re-inserts a node by walking
 * while `v > next` / `v < prev`, and with v left at double precision it sits
 * between its own stored copy and a neighbour and walks back and forth
 * forever, which froze every race at the start line.
 *
 * PC narrows the significand only; the exponent keeps the register's range.
 * So a plain (float) cast is right only inside float's range -- outside it,
 * it would turn a large intermediate into inf (and inf*0 into NaN) or flush a
 * tiny one to 0, where the x87 carries on. Those go the long way. */
/* Option C, F4: the range test reads the double's exponent field - one integer
 * compare instead of fabs and four float compares - with bit-identical results
 * (tests/fp_round24 checks them against the previous version):
 *   exponent 897..1149 (2^-126 <= |x| < 2^127)  float's normal range: a cast
 *   exponent 1150      (2^127 <= |x| < 2^128)   a cast only up to FLT_MAX;
 *                                                above it (float) would be inf
 *   exponent 0x7FF (inf, NaN) and +-0           a cast, as before
 *   anything else - the long way, as before.
 * F4b: only the float-normal range test stays inline at each x87 site; the rest
 * (+-0 included: a zero test inline cost a compare and two branches per site
 * and made F3' slower, 1.42 x B) is one out-of-line cold function. Inlined at every site (54 in one Black
 * function), the frexp/ldexp calls made the hot code 2.4 x larger than B's and
 * forced register spills around them, although they almost never run. */
#if defined(_MSC_VER) && !defined(__clang__)
#define RECOMP_COLD_NOINLINE __declspec(noinline)
#define RECOMP_LIKELY(c) (c)
#else
#define RECOMP_COLD_NOINLINE __attribute__((noinline, cold, unused))
#define RECOMP_LIKELY(c) __builtin_expect(!!(c), 1)
#endif
/* static + noinline: one private copy per translation unit that uses it, so
 * the header stays self-contained (no runtime symbol to link). Not "inline" as
 * well: GCC rejects inline + noinline (-Werror=attributes); "unused" keeps a TU
 * that never calls it quiet. */
static RECOMP_COLD_NOINLINE double recomp_fp_round24_slow(double x) {
    uint64_t b;
    uint32_t e;
    int ex;
    memcpy(&b, &x, sizeof b);
    e = (uint32_t)(b >> 52) & 0x7FFu;
    if ((e == 1150u && (b & 0x7FFFFFFFFFFFFFFFull) <= 0x47EFFFFFE0000000ull)
        || e == 0x7FFu || (b << 1) == 0)
        return (double)(float)x;
    x = frexp(x, &ex);                     /* |x| in [0.5, 1): float-exact range */
    return ldexp((double)(float)x, ex);
}
static inline double recomp_fp_round24(double x) {
    uint64_t b;
    memcpy(&b, &x, sizeof b);
    if (RECOMP_LIKELY(((uint32_t)(b >> 52) & 0x7FFu) - 897u < 253u))
        return (double)(float)x;
    return recomp_fp_round24_slow(x);
}
#define RECOMP_FP_PC(x) ((g_fp_control_word & 0x300u) ? (double)(x) : recomp_fp_round24(x))
#define RECOMP_FCMP_CC(c) ((uint16_t)((c)==2 ? 0x4500u : (c)<0 ? 0x0100u : (c)>0 ? 0u : 0x4000u))
/* Values in the existing double-backed stack are all representable as normal
 * x87 extended values, including binary64 subnormals. Empty stack tags and
 * unsupported extended encodings are not represented by this stack model. */
static inline uint16_t recomp_fxam(double value) {
    return (uint16_t)((signbit(value) ? 0x0200u : 0u) |
        (isnan(value) ? 0x0100u : isinf(value) ? 0x0500u :
         value == 0.0 ? 0x4000u : 0x0400u));
}

/* Result of an x87 compare, in the shape the status word wants:
 *   -1 less, 0 equal, 1 greater, 2 unordered (either operand is NaN).
 * The unordered case is not a curiosity: `fucompp` of a value with itself
 * followed by `test ah, 0x44; jp` is how this era's CRT asks "is this a NaN",
 * and collapsing it to "equal" answers no every time. */
#define RECOMP_FCMP(a, b)     (((a) != (a) || (b) != (b)) ? 2 : (a) < (b) ? -1 : (a) > (b) ? 1 : 0)
/* x87 rounding to an integral value under the guest RC bits (control word
 * bits 10-11: 0 nearest-even, 1 down, 2 up, 3 toward zero). FRNDINT rounds
 * this way, and so does FIST before its store; neither may follow the HOST's
 * rounding mode, which the guest's fldcw never reaches. MSVC's CRT floor() and
 * ceil() are `fldcw RC=down/up` (via _ctrlfp) around _frnd, a bare frndint,
 * so a frndint lifted as rint() made both of them round to nearest.
 * NaN, the infinities and values already integral come back unchanged, and a
 * zero result keeps the operand's sign, as FRNDINT's does. */
static inline double recomp_frndint(double value, uint16_t control) {
    double rounded;
    if (!isfinite(value)) return value;
    switch((control>>10)&3) {
    case 1: rounded=floor(value); break;
    case 2: rounded=ceil(value); break;
    case 3: rounded=trunc(value); break;
    default: {
        double lo=floor(value), fraction=value-lo;
        rounded=lo;
        if(fraction>0.5 || (fraction==0.5 && fmod(lo,2.0)!=0.0)) rounded=lo+1.0;
        break;
    }
    }
    return rounded == 0.0 ? copysign(0.0, value) : rounded;
}
/* x87 integer stores use the guest RC bits, independently of host rounding.
 * Masked invalid conversions store the signed integer-indefinite value. */
static inline int64_t recomp_fist(double value, uint16_t control, unsigned bits) {
    double rounded = recomp_frndint(value, control);
    double limit=ldexp(1.0,(int)bits-1);
    if(!isfinite(rounded) || rounded < -limit || rounded >= limit)
        return bits==64?INT64_MIN:-(INT64_C(1)<<(bits-1));
    return (int64_t)rounded;
}

/* CVTSS2SI/CVTSD2SI (and the truncating CVTT forms) to a 32-bit integer
 * (sp00n c50c597, Kitae Noh). MXCSR (X86Ctx.mxcsr, reset 0x1F80: every
 * exception masked, round to nearest) is guest state like the x87 control
 * word: STMXCSR stores it, LDMXCSR loads it, the host's MXCSR is never
 * changed. Its exception flags (bits 0-5) are not modelled and read as clear.
 * The non-truncating forms round under MXCSR.RC (bits 13-14), which uses the
 * x87 RC encoding, so recomp_frndint does the rounding. A C cast would always
 * truncate, and is undefined out of range; the hardware answers NaN, the
 * infinities and out-of-range values with the integer indefinite. Takes the
 * MXCSR value, not ctx: a pure function of its arguments. */
static inline int32_t recomp_cvt_si32(double value, uint32_t mxcsr, int truncate) {
    double rounded = truncate ? (isfinite(value) ? trunc(value) : value)
                              : recomp_frndint(value, (uint16_t)(((mxcsr >> 13) & 3u) << 10));
    if (!(rounded >= -2147483648.0 && rounded <= 2147483647.0))
        return (int32_t)0x80000000u;
    return (int32_t)rounded;
}

/* fld/fstp tbyte (sp00n b40805c): 64-bit significand with an explicit integer
 * bit, 15-bit exponent biased by 16383, sign on top. The stack is double, so a
 * load rounds to 53 bits and a store is exact. Pure functions of their
 * arguments: no register state, so nothing here needs ctx. */
static inline double recomp_f80_load(uint64_t mant, uint16_t se) {
    int e = se & 0x7fff;
    double v = e == 0x7fff ? ((mant << 1) ? NAN : INFINITY)
                           : ldexp((double)mant, e - 16383 - 63);
    return (se & 0x8000u) ? -v : v;
}
static inline uint16_t recomp_f80_store(double v, uint64_t *mant) {
    union { double d; uint64_t u; } bits;
    uint16_t sign = signbit(v) ? 0x8000u : 0u;
    int e;
    bits.d = v;
    if (v != v) { *mant = 0x8000000000000000ull | (bits.u << 11); return sign | 0x7fffu; }
    if (isinf(v)) { *mant = 0x8000000000000000ull; return sign | 0x7fffu; }
    if (v == 0.0) { *mant = 0; return sign; }
    v = frexp(fabs(v), &e);
    *mant = (uint64_t)(int64_t)ldexp(v, 63) << 1;
    return (uint16_t)(sign | (e + 16382));
}

/* x87 trig, remainder and scale edge cases (sp00n 999a8c6). The two that set
 * condition bits take the register set: g_fp_cc is ctx->fp_cc (option C,
 * C.1), so upstream's global-writing helpers get ctx as their first argument,
 * like every runtime function that touches guest registers.
 *
 * fsin/fcos/fsincos/fptan: an operand of magnitude 2^63 or more is out of
 * range -- the hardware sets C2 and leaves the operand (and the stack depth)
 * unchanged. A completed one clears C2. */
static inline int recomp_fp_trig_in_range(X86Ctx *ctx, double value) {
    if (isfinite(value) && fabs(value) >= 0x1p63) {
        g_fp_cc |= 0x0400u;
        return 0;
    }
    g_fp_cc &= (uint16_t)~0x0400u;
    return 1;
}

/* fscale: st0 * 2^trunc(st1), with the infinite and NaN scales the x87
 * defines, and no undefined float-to-int cast for huge finite scales. */
static inline double recomp_fscale(double value, double scale) {
    if (isnan(value) || isnan(scale)) return value + scale;
    if (scale == INFINITY)
        return value == 0.0 ? NAN : copysign(INFINITY, value);
    if (scale == -INFINITY)
        return isinf(value) ? NAN : copysign(0.0, value);
    /* Finite exponents beyond this clamp already overflow/underflow double.
     * Avoid an undefined float-to-int cast for the much larger x87 inputs. */
    return scalbn(value, scale > 4096 ? 4096 : scale < -4096 ? -4096 : (int)scale);
}

/* fprem (nearest = 0, quotient chopped) and fprem1 (nearest = 1, IEEE).
 * C0/C3/C1 get quotient bits 2/1/0 -- the CRT's sin/cos argument reduction
 * picks its octant from them -- and C2 says whether the reduction is
 * complete. */
static inline double recomp_fprem(X86Ctx *ctx, double a, double b, int nearest) {
    int quotient = 0, ea = 0, eb = 0;
    unsigned q;
    double result;
    if (isfinite(a) && isfinite(b) && a != 0.0 && b != 0.0) {
        frexp(a, &ea); frexp(b, &eb);
        if (ea - eb >= 64) {
            /* Intel permits 32..63 bits of quotient per partial reduction.
             * Keep C2 set; the guest repeats until a complete reduction. */
            g_fp_cc |= 0x0400u;
            return fmod(a, scalbn(b, ea - eb - 32));
        }
    }
    result = remquo(a, b, &quotient);
    /* remquo's quotient is round-to-nearest; FPREM chops. Step back one in
     * magnitude, on the low bits remquo returns, before reading C0/C3/C1. */
    q = (unsigned)(quotient < 0 ? -quotient : quotient);
    /* !signbit on both sides: signbit's nonzero value is unspecified. */
    if (!nearest && result != 0.0 && isfinite(result) && !signbit(result) != !signbit(a)) {
        result += copysign(fabs(b), a);
        q -= 1u;
    }
    /* C0/C3/C1 contain quotient bits 2/1/0; C2 says complete. */
    g_fp_cc = (uint16_t)(((q & 4u) << 6) | ((q & 2u) << 13) | ((q & 1u) << 9));
    return result;
}

/* ================================================================
 * ICALL trace ring buffer (for debugging indirect calls)
 * ================================================================ */

/** Size of the ring buffer (must be power of 2). */
#define ICALL_TRACE_SIZE 16

/** Ring buffer of recent indirect call target VAs. */
extern volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE];

/** Current write index into the ring buffer. */
extern volatile uint32_t g_icall_trace_idx;

/** Total count of indirect calls executed. */
extern volatile uint64_t g_icall_count;

/*
 * Where RECOMP_ICALL records its targets (option C, F1). The ring above and the
 * counter are process-wide `volatile`: every indirect call of every guest
 * thread wrote the same cache lines, which made dispatch ~8x slower with four
 * threads (69 vs 8.7 ns/call, docs/option_c.md). Three modes, chosen at
 * compile time:
 *   release (NDEBUG, CMake Release) - no trace at all, the default for builds
 *       that ship or are measured;
 *   RECOMP_ICALL_TRACE=1, or a build without NDEBUG - each thread keeps its own
 *       ring (RECOMP_TLS, plain stores), nothing counted; a crash handler runs
 *       on the faulting thread and reads it through recomp_icall_trace_at();
 *   RECOMP_ICALL_TRACE_SHARED=1 - the old shared ring + counter, for hunting a
 *       hang across threads (the watchdog can read it).
 * The shared globals stay defined in every mode, so game code naming them builds.
 */
#if defined(RECOMP_ICALL_TRACE_SHARED) && RECOMP_ICALL_TRACE_SHARED
#define RECOMP_ICALL_TRACE_PUT(_va) do { \
    g_icall_trace[g_icall_trace_idx & (ICALL_TRACE_SIZE-1)] = (_va); \
    g_icall_trace_idx++; \
    g_icall_count++; \
} while (0)
#elif defined(NDEBUG) && !(defined(RECOMP_ICALL_TRACE) && RECOMP_ICALL_TRACE)
#define RECOMP_ICALL_TRACE_PUT(_va) ((void)(_va))
#else
extern RECOMP_TLS uint32_t g_icall_tls_trace[ICALL_TRACE_SIZE];
extern RECOMP_TLS uint32_t g_icall_tls_idx;
#define RECOMP_ICALL_TRACE_PUT(_va) do { \
    g_icall_tls_trace[g_icall_tls_idx & (ICALL_TRACE_SIZE-1)] = (_va); \
    g_icall_tls_idx++; \
} while (0)
#endif

/** The calling thread's n-th most recent indirect-call target (0 = latest), from whichever ring is in use. */
uint32_t recomp_icall_trace_at(unsigned back);

/**
 * Called when an indirect call target cannot be resolved.
 * Implement this in your game-specific code to log diagnostics.
 * The va parameter is the Xbox VA that failed to resolve.
 */
void recomp_icall_fail_log(X86Ctx *ctx, uint32_t va);

/* Report an indirect call whose target is not code (a null or wild
 * function pointer). Rate-limited per address by the implementation,
 * because these usually arrive inside a loop -- which is exactly why
 * they must be reported: silently skipping one turns a diagnosable null
 * vtable call into an unexplained hang. */
void recomp_icall_not_code_log(X86Ctx *ctx, uint32_t va);

/* Indirect-branch target feedback. The ring buffer above is crash forensics --
 * 16 entries, overwritten constantly. This is a durable, deduplicated record of
 * every target the title ever reached, for feeding back into the next codegen
 * run (tools/recomp/icall_feedback.py).
 *
 * The header is pulled in only when the feature is on, so a default build needs
 * neither the file nor src/kernel on its include path. Disabled,
 * RECOMP_ICALL_OBSERVE discards its arguments without expanding them, so the
 * RECOMP_ICALL_SEEN_* constants need not exist either. */
#ifdef RECOMP_ICALL_FEEDBACK
#include "recomp_icall_feedback.h"
#else
#define RECOMP_ICALL_OBSERVE(va, flags) ((void)0)
#define RECOMP_ICALL_OBSERVE_SITE(site, va) ((void)0)
#endif

/**
 * Function entry trace, emitted only for addresses passed to
 * tools.recomp --trace-functions. Bring-up is largely "which of these
 * init calls does it not come back from", and answering that by
 * overriding a function loses the body you were trying to observe.
 */
/* The guest's time source.
 *
 * Xbox's QueryPerformanceCounter is a bare rdtsc and its
 * QueryPerformanceFrequency returns the CPU clock as a constant, so the guest
 * divides this by 733,333,333 to get seconds. Returning the host TSC would
 * make that division wrong by the ratio of the two clocks; the runtime scales
 * to the console's rate instead. */
uint64_t xbox_ReadTimeStampCounter(void);

/* x86 port I/O (`in`/`out`), width 1, 2 or 4 bytes: the runtime models the ports titles are known to read (the
 * video encoder's field pin at 0x80C0) and logs the first access to any other (kernel_hal.c). */
uint32_t xbox_PortIn(uint32_t port, int width);
void     xbox_PortOut(uint32_t port, uint32_t value, int width);

/* The trace hooks, the ICALL failure hooks and recomp_unimpl get the calling
 * function's register set (option C): they may report registers, and a ctx is
 * the only way generated code hands them over. */
void recomp_trace_enter(X86Ctx *ctx, const char *name, uint32_t va);
#define RECOMP_TRACE_ENTER(name, va) recomp_trace_enter(ctx, (name), (va))
void recomp_trace_exit(X86Ctx *ctx, const char *name, uint32_t va);
#define RECOMP_TRACE_EXIT(name, va) recomp_trace_exit(ctx, (name), (va))
void recomp_trace_esp(X86Ctx *ctx, const char *name, const char *tag);
#define RECOMP_TRACE_ESP(name, tag) recomp_trace_esp(ctx, (name), (tag))


/* ================================================================
 * Memory access helpers
 * ================================================================ */

/**
 * Translate an Xbox VA to an actual pointer.
 * Mask to 32-bit first: Xbox addresses are 32-bit and arithmetic
 * in the recompiled code can overflow. Without the mask, a 64-bit
 * uintptr_t cast preserves the overflow bits, landing us 4GB+ past
 * our mapping and causing access violations.
 */
#define XBOX_PTR(addr) ((uintptr_t)(uint32_t)(addr) + g_xbox_mem_offset)

/* Guest memory access (option C, C.2).
 *
 * The element types may alias anything and may be unaligned -- the guest is
 * an x86 that does both -- so the compiler neither assumes type-based
 * non-aliasing nor emits alignment-dependent loads (ARM64-safe).
 *
 * RECOMP_MEMQ qualifies every access. It is volatile by default -- runtime code
 * and anything compiled outside generated code -- and in generated code for
 * functions in the XDK library sections (D3D, DSOUND, XMV, ... from the XBE
 * section table: they talk to "hardware" -- the pushbuffer PUT/GET, status
 * registers -- that another host thread updates). Game code (.text) gets plain
 * accesses the compiler may keep in host registers; RECOMP_BACKEDGE() on every
 * backward branch makes a polling loop re-read memory each round, so a value
 * another thread stores is still seen. The lifter switches RECOMP_MEMQ around
 * each library function. VMEM* are always volatile (MMIO paths of rep movs). */
#if defined(__GNUC__) || defined(__clang__)
typedef uint8_t  __attribute__((aligned(1), may_alias)) recomp_u8;
typedef uint16_t __attribute__((aligned(1), may_alias)) recomp_u16;
typedef uint32_t __attribute__((aligned(1), may_alias)) recomp_u32;
typedef int8_t   __attribute__((aligned(1), may_alias)) recomp_s8;
typedef int16_t  __attribute__((aligned(1), may_alias)) recomp_s16;
typedef int32_t  __attribute__((aligned(1), may_alias)) recomp_s32;
typedef int64_t  __attribute__((aligned(1), may_alias)) recomp_s64;
typedef float    __attribute__((aligned(1), may_alias)) recomp_f32;
typedef double   __attribute__((aligned(1), may_alias)) recomp_f64;
#define RECOMP_BACKEDGE() __asm__ __volatile__("" ::: "memory")
#else   /* MSVC (32-bit conformance oracle): x86 does unaligned, MSVC no strict aliasing */
typedef uint8_t  recomp_u8;
typedef uint16_t recomp_u16;
typedef uint32_t recomp_u32;
typedef int8_t   recomp_s8;
typedef int16_t  recomp_s16;
typedef int32_t  recomp_s32;
typedef int64_t  recomp_s64;
typedef float    recomp_f32;
typedef double   recomp_f64;
#define RECOMP_BACKEDGE() _ReadWriteBarrier()
#endif
/* pause (F3 90), the spin-wait hint: the host's own hint, so a polling loop
 * does not starve the thread it waits for of a shared core. Visibility of the
 * polled store comes from RECOMP_BACKEDGE, not from this. */
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#define RECOMP_PAUSE() _mm_pause()
#elif defined(_MSC_VER) && defined(_M_ARM64)
#define RECOMP_PAUSE() __yield()
#elif defined(__i386__) || defined(__x86_64__)
#define RECOMP_PAUSE() __builtin_ia32_pause()
#elif defined(__aarch64__)
#define RECOMP_PAUSE() __asm__ __volatile__("yield")
#else
#define RECOMP_PAUSE() ((void)0)
#endif
#ifndef RECOMP_MEMQ
#define RECOMP_MEMQ volatile
#endif
/* The host address behind a MEM* access. Plain XBOX_PTR here; in generated
 * code the game form (RECOMP_GAME_GPTR) or, around XDK library functions, the
 * form that release-fences a store to the NV2A USER area (RECOMP_LIB_GPTR) --
 * both defined at the end of this header. */
#ifndef RECOMP_GPTR
#define RECOMP_GPTR(addr) XBOX_PTR(addr)
#endif

/** Read/write N bytes at a flat Xbox memory address. */
#define MEM8(addr)   (*(RECOMP_MEMQ recomp_u8  *)RECOMP_GPTR(addr))
#define MEM16(addr)  (*(RECOMP_MEMQ recomp_u16 *)RECOMP_GPTR(addr))
#define MEM32(addr)  (*(RECOMP_MEMQ recomp_u32 *)RECOMP_GPTR(addr))

/** Signed memory reads. */
#define SMEM8(addr)  (*(RECOMP_MEMQ recomp_s8  *)RECOMP_GPTR(addr))
#define SMEM16(addr) (*(RECOMP_MEMQ recomp_s16 *)RECOMP_GPTR(addr))
#define SMEM32(addr) (*(RECOMP_MEMQ recomp_s32 *)RECOMP_GPTR(addr))
#define SMEM64(addr) (*(RECOMP_MEMQ recomp_s64 *)RECOMP_GPTR(addr))

/** Float/double memory access. */
#define MEMF(addr)   (*(RECOMP_MEMQ recomp_f32 *)RECOMP_GPTR(addr))
#define MEMD(addr)   (*(RECOMP_MEMQ recomp_f64 *)RECOMP_GPTR(addr))

/** Always volatile, whatever RECOMP_MEMQ says. */
#define VMEM8(addr)  (*(volatile recomp_u8  *)XBOX_PTR(addr))
#define VMEM16(addr) (*(volatile recomp_u16 *)XBOX_PTR(addr))
#define VMEM32(addr) (*(volatile recomp_u32 *)XBOX_PTR(addr))

/* ================================================================
 * SSE / XMM register state
 *
 * XMM is 128 bits of architectural state, not a scalar float. Modelling
 * it as a `float` made movaps/movups transfer 4 of 16 bytes and silently
 * drop the upper three lanes, and left the packed arithmetic with no
 * representation at all.
 *
 * The registers are global for the same reason the volatile GPRs are:
 * one guest routine can lift to several C functions, so a value produced
 * in one body and read in the next has to outlive the body that wrote it.
 * A function-local declaration would also shadow these, and the local
 * starts zeroed -- a returned float would silently read as 0.0.
 *
 * The helpers are lane-wise C rather than host intrinsics: the guest
 * semantics stay explicit (MINPS returning src on unordered, CMPNEQPS
 * being the unordered form) and the header stays portable.
 * ================================================================ */



/* -- construction -- */

static inline RecompXmm XMM_ZERO(void) {
    RecompXmm r; r.q[0] = 0; r.q[1] = 0; return r;
}

/** movss from memory: lane 0 set, upper lanes zeroed. */
static inline RecompXmm XMM_SCALAR(float v) {
    RecompXmm r = XMM_ZERO(); r.f[0] = v; return r;
}

/** movsd from memory: low double set, high double zeroed. */
static inline RecompXmm XMM_SCALAR_DOUBLE(double v) {
    RecompXmm r = XMM_ZERO(); r.d[0] = v; return r;
}

/** movd: 32 raw bits into lane 0, upper lanes zeroed. */
static inline RecompXmm XMM_SCALAR_BITS(uint32_t bits) {
    RecompXmm r = XMM_ZERO(); r.u[0] = bits; return r;
}

/* -- guest memory --
 * Addresses are guest VAs, so they go through MEM32 like every other
 * access. Done lane-wise, which is also unaligned-safe for movups. */

/* Macros, not functions taking the address (option C, C.2b): every lane is a
 * MEM32 at the use site, so generated code gets its own guest-pointer form
 * (base + addr, provenance kept) and its own qualifier -- plain in game code,
 * volatile in XDK library code -- like any other access. As inline functions
 * they were compiled once with the runtime form: volatile, integer offset.
 * The address expressions the lifter passes have no side effects; XMM_MEM
 * evaluates its address four times. */
static inline RecompXmm recomp_xmm_from_u32(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    RecompXmm r;
    r.u[0] = a; r.u[1] = b; r.u[2] = c; r.u[3] = d;
    return r;
}
#define XMM_MEM(addr) recomp_xmm_from_u32(MEM32(addr), MEM32((addr) + 4), \
                                          MEM32((addr) + 8), MEM32((addr) + 12))
#define XMM_STORE(addr, v) do { RecompXmm _xv = (v); uint32_t _xa = (uint32_t)(addr); \
    MEM32(_xa) = _xv.u[0];      MEM32(_xa + 4) = _xv.u[1]; \
    MEM32(_xa + 8) = _xv.u[2];  MEM32(_xa + 12) = _xv.u[3]; } while (0)

/* movlps/movhps move 8 bytes into or out of one half, leaving the
 * other half alone. */
#define RECOMP_XMM_LOAD_HALF(dst, addr, h) do { uint32_t _xa = (uint32_t)(addr); \
    (dst).u[(h) * 2] = MEM32(_xa); (dst).u[(h) * 2 + 1] = MEM32(_xa + 4); } while (0)
#define RECOMP_XMM_STORE_HALF(addr, src, h) do { RecompXmm _xv = (src); uint32_t _xa = (uint32_t)(addr); \
    MEM32(_xa) = _xv.u[(h) * 2]; MEM32(_xa + 4) = _xv.u[(h) * 2 + 1]; } while (0)
#define XMM_LOAD_LOW(dst, addr)   RECOMP_XMM_LOAD_HALF(dst, addr, 0)
#define XMM_LOAD_HIGH(dst, addr)  RECOMP_XMM_LOAD_HALF(dst, addr, 1)
#define XMM_STORE_LOW(addr, src)  RECOMP_XMM_STORE_HALF(addr, src, 0)
#define XMM_STORE_HIGH(addr, src) RECOMP_XMM_STORE_HALF(addr, src, 1)

/** movlhps: dst high = src low. */
static inline RecompXmm XMM_MOVE_LOW_TO_HIGH(RecompXmm a, RecompXmm b) {
    RecompXmm r; r.q[0] = a.q[0]; r.q[1] = b.q[0]; return r;
}

/** movhlps: dst low = src high. */
static inline RecompXmm XMM_MOVE_HIGH_TO_LOW(RecompXmm a, RecompXmm b) {
    RecompXmm r; r.q[0] = b.q[1]; r.q[1] = a.q[1]; return r;
}

/* -- packed arithmetic -- */

#define RECOMP_XMM_LANEWISE(name, expr)                                   \
    static inline RecompXmm name(RecompXmm a, RecompXmm b) {              \
        RecompXmm r; int i;                                               \
        for (i = 0; i < 4; ++i) { (void)a; (void)b; r.f[i] = (expr); }    \
        return r;                                                         \
    }

RECOMP_XMM_LANEWISE(XMM_ADD, a.f[i] + b.f[i])
RECOMP_XMM_LANEWISE(XMM_SUB, a.f[i] - b.f[i])
RECOMP_XMM_LANEWISE(XMM_MUL, a.f[i] * b.f[i])
RECOMP_XMM_LANEWISE(XMM_DIV, a.f[i] / b.f[i])
/* MINPS/MAXPS return the second operand when the lanes are unordered or
 * equal -- that is the hardware's tie-break, not a C fmin/fmax. */
RECOMP_XMM_LANEWISE(XMM_MIN, (a.f[i] < b.f[i]) ? a.f[i] : b.f[i])
RECOMP_XMM_LANEWISE(XMM_MAX, (a.f[i] > b.f[i]) ? a.f[i] : b.f[i])

#define RECOMP_XMM_BITWISE(name, expr)                                    \
    static inline RecompXmm name(RecompXmm a, RecompXmm b) {              \
        RecompXmm r; int i;                                               \
        for (i = 0; i < 4; ++i) { (void)a; (void)b; r.u[i] = (expr); }    \
        return r;                                                         \
    }

RECOMP_XMM_BITWISE(XMM_AND,  a.u[i] & b.u[i])
RECOMP_XMM_BITWISE(XMM_OR,   a.u[i] | b.u[i])
RECOMP_XMM_BITWISE(XMM_XOR,  a.u[i] ^ b.u[i])
/* ANDNPS is ~dst & src, not dst & ~src. */
RECOMP_XMM_BITWISE(XMM_ANDN, (~a.u[i]) & b.u[i])

/* Compares produce an all-ones or all-zero mask per lane. EQ/LT/LE are
 * the ordered forms (false when either lane is NaN); NEQ is the
 * unordered form, so it is true when a lane is NaN. */
RECOMP_XMM_BITWISE(XMM_CMP_EQ,  (a.f[i] == b.f[i]) ? 0xFFFFFFFFu : 0u)
RECOMP_XMM_BITWISE(XMM_CMP_LT,  (a.f[i] <  b.f[i]) ? 0xFFFFFFFFu : 0u)
RECOMP_XMM_BITWISE(XMM_CMP_LE,  (a.f[i] <= b.f[i]) ? 0xFFFFFFFFu : 0u)
RECOMP_XMM_BITWISE(XMM_CMP_NEQ, (a.f[i] == b.f[i]) ? 0u : 0xFFFFFFFFu)

/* The full SSE compare predicate set, by CMPPS/CMPSS immediate:
 * 0 EQ, 1 LT, 2 LE, 3 UNORD, 4 NEQ, 5 NLT, 6 NLE, 7 ORD. The N forms are
 * the negations, so they are true when either side is NaN. */
static inline int recomp_cmp_pred(float a, float b, int p) {
    switch (p & 7) {
    case 0:  return a == b;
    case 1:  return a < b;
    case 2:  return a <= b;
    case 3:  return a != a || b != b;
    case 4:  return !(a == b);
    case 5:  return !(a < b);
    case 6:  return !(a <= b);
    default: return !(a != a || b != b);
    }
}
static inline RecompXmm XMM_CMP_PRED(RecompXmm a, RecompXmm b, int p) {
    RecompXmm r; int i;
    for (i = 0; i < 4; ++i)
        r.u[i] = recomp_cmp_pred(a.f[i], b.f[i], p) ? 0xFFFFFFFFu : 0u;
    return r;
}
/* Packed unary ops; the first argument is unused, as for the binary forms. */
RECOMP_XMM_LANEWISE(XMM_SQRT,  sqrtf(b.f[i]))
RECOMP_XMM_LANEWISE(XMM_RSQRT, 1.0f / sqrtf(b.f[i]))
RECOMP_XMM_LANEWISE(XMM_RCP,   1.0f / b.f[i])

/** movmskps: the four lane sign bits, packed into the low nibble. */
static inline uint32_t XMM_MOVEMASK(RecompXmm a) {
    return ((a.u[0] >> 31) & 1u) | (((a.u[1] >> 31) & 1u) << 1)
         | (((a.u[2] >> 31) & 1u) << 2) | (((a.u[3] >> 31) & 1u) << 3);
}

/** shufps: lanes 0-1 selected out of `a`, lanes 2-3 out of `b`. */
static inline RecompXmm XMM_SHUFFLE(RecompXmm a, RecompXmm b, uint32_t imm) {
    RecompXmm r;
    r.u[0] = a.u[(imm >> 0) & 3u]; r.u[1] = a.u[(imm >> 2) & 3u];
    r.u[2] = b.u[(imm >> 4) & 3u]; r.u[3] = b.u[(imm >> 6) & 3u];
    return r;
}

/** unpcklps / unpckhps: interleave the low or high halves. */
static inline RecompXmm XMM_UNPACK_LOW(RecompXmm a, RecompXmm b) {
    RecompXmm r;
    r.u[0] = a.u[0]; r.u[1] = b.u[0]; r.u[2] = a.u[1]; r.u[3] = b.u[1];
    return r;
}

static inline RecompXmm XMM_UNPACK_HIGH(RecompXmm a, RecompXmm b) {
    RecompXmm r;
    r.u[0] = a.u[2]; r.u[1] = b.u[2]; r.u[2] = a.u[3]; r.u[3] = b.u[3];
    return r;
}

/* ================================================================
 * Flag computation helpers
 *
 * These macros compute x86 flags for conditional branches.
 * Used by the lifter's pattern-matching output:
 *   cmp a, b; jcc target  ->  if (COND(a, b)) goto target;
 * ================================================================ */

/* Unsigned comparison conditions (from CMP a, b -> a - b) */
#define CMP_EQ(a, b)  ((uint32_t)(a) == (uint32_t)(b))
#define CMP_NE(a, b)  ((uint32_t)(a) != (uint32_t)(b))
#define CMP_B(a, b)   ((uint32_t)(a) <  (uint32_t)(b))   /* below (CF=1) */
#define CMP_AE(a, b)  ((uint32_t)(a) >= (uint32_t)(b))   /* above or equal */
#define CMP_BE(a, b)  ((uint32_t)(a) <= (uint32_t)(b))   /* below or equal */
#define CMP_A(a, b)   ((uint32_t)(a) >  (uint32_t)(b))   /* above */

/* Signed comparison conditions */
/* x86 evaluates the signed conditions at the operand width, not at 32 bits.
   The generated code passes LO8/HI8/LO16 sub-register reads straight in, and
   those zero-extend, so recover the width and sign-extend before comparing. */
#define RECOMP_FLAG_WIDTH(a, b) (sizeof(a) < sizeof(b) ? sizeof(a) : sizeof(b))
#define RECOMP_SIGNED(value, width) \
    ((width) == 1u ? (int32_t)(int8_t)(uint8_t)(uint32_t)(value) \
     : (width) == 2u ? (int32_t)(int16_t)(uint16_t)(uint32_t)(value) \
     : (int32_t)(uint32_t)(value))
#define CMP_L(a, b)   (RECOMP_SIGNED(a, RECOMP_FLAG_WIDTH(a, b)) <  \
                       RECOMP_SIGNED(b, RECOMP_FLAG_WIDTH(a, b)))  /* less */
#define CMP_GE(a, b)  (RECOMP_SIGNED(a, RECOMP_FLAG_WIDTH(a, b)) >= \
                       RECOMP_SIGNED(b, RECOMP_FLAG_WIDTH(a, b)))  /* >= */
#define CMP_LE(a, b)  (RECOMP_SIGNED(a, RECOMP_FLAG_WIDTH(a, b)) <= \
                       RECOMP_SIGNED(b, RECOMP_FLAG_WIDTH(a, b)))  /* <= */
#define CMP_G(a, b)   (RECOMP_SIGNED(a, RECOMP_FLAG_WIDTH(a, b)) >  \
                       RECOMP_SIGNED(b, RECOMP_FLAG_WIDTH(a, b)))  /* > */

/* TEST-based conditions (AND without storing result) */
#define TEST_Z(a, b)  (((uint32_t)(a) & (uint32_t)(b)) == 0)  /* ZF=1 */
#define TEST_NZ(a, b) (((uint32_t)(a) & (uint32_t)(b)) != 0)  /* ZF=0 */
#define TEST_S(a, b)  (RECOMP_SIGNED((uint32_t)(a) & (uint32_t)(b), \
                                     RECOMP_FLAG_WIDTH(a, b)) < 0)  /* SF=1 */

/* ================================================================
 * Arithmetic with carry/overflow detection
 * ================================================================ */

/** Add with carry flag. Returns result, sets *cf. */
static inline uint32_t ADD32_CF(uint32_t a, uint32_t b, int *cf) {
    uint32_t r = a + b;
    *cf = (r < a);
    return r;
}

/** Sub with carry (borrow) flag. Returns result, sets *cf. */
static inline uint32_t SUB32_CF(uint32_t a, uint32_t b, int *cf) {
    *cf = (a < b);
    return a - b;
}

/* ================================================================
 * Rotation / shift helpers
 * ================================================================ */

/* x86 masks the rotate count to 5 bits, and THEN the rotate is modulo the
 * operand's own width -- so `rol al, 16` is a rotate by zero and `rol ax, 31`
 * is a rotate by 15. A narrow rotate performed at 32 bits is not a rotate at
 * all: the bits that should wrap around at bit 7 or 15 land above the operand
 * and are discarded by the store, which turns `ror al, 2` on 0x01 into 0x00
 * where x86 gives 0x40.
 *
 * The zero case is separated out because `val >> (32 - 0)` is a shift of a
 * uint32_t by 32, which is undefined behaviour -- it happened to survive
 * because x86 masks shift counts to 5 bits and gives back `val`, but the
 * compiler is under no obligation to agree, least of all at -O2. */
static inline uint32_t ROL32(uint32_t val, int n) {
    n &= 31;
    return n ? ((val << n) | (val >> (32 - n))) : val;
}

static inline uint32_t ROR32(uint32_t val, int n) {
    n &= 31;
    return n ? ((val >> n) | (val << (32 - n))) : val;
}

/* rcl/rcr: rotate through the carry flag.
 *
 * The carry is a bit sitting one place above the operand's top bit, so the
 * rotation is over width+1 bits. That is also why a count is reduced modulo
 * width+1 for the 8- and 16-bit forms rather than modulo the width: a byte
 * rotates through nine positions, not eight. The 32-bit form takes the count
 * masked to five bits and no further, which is already inside 33.
 *
 * `cf` carries in and out.
 */
static inline uint32_t RC_ROT(uint32_t val, unsigned n, int *cf,
                              unsigned width, int left) {
    unsigned mod = width + 1u;
    uint64_t mask = (width >= 32u) ? 0xFFFFFFFFull
                                   : ((((uint64_t)1 << width) - 1u));
    uint64_t x = (((uint64_t)(*cf & 1)) << width) | ((uint64_t)val & mask);

    n &= 31u;
    if (width < 32u)
        n %= mod;
    if (n) {
        uint64_t full = (((uint64_t)1 << mod) - 1u);
        x = left ? ((x << n) | (x >> (mod - n)))
                 : ((x >> n) | (x << (mod - n)));
        x &= full;
    }
    *cf = (int)((x >> width) & 1);
    return (uint32_t)(x & mask);
}

static inline uint8_t ROL8(uint8_t val, int n) {
    n = (n & 31) % 8;
    return n ? (uint8_t)((val << n) | (val >> (8 - n))) : val;
}

static inline uint8_t ROR8(uint8_t val, int n) {
    n = (n & 31) % 8;
    return n ? (uint8_t)((val >> n) | (val << (8 - n))) : val;
}

static inline uint16_t ROL16(uint16_t val, int n) {
    n = (n & 31) % 16;
    return n ? (uint16_t)((val << n) | (val >> (16 - n))) : val;
}

static inline uint16_t ROR16(uint16_t val, int n) {
    n = (n & 31) % 16;
    return n ? (uint16_t)((val >> n) | (val << (16 - n))) : val;
}

/* ================================================================
 * Sign/zero extension
 * ================================================================ */

#define ZX8(v)   ((uint32_t)(uint8_t)(v))
#define ZX16(v)  ((uint32_t)(uint16_t)(v))
#define SX8(v)   ((uint32_t)(int32_t)(int8_t)(v))
#define SX16(v)  ((uint32_t)(int32_t)(int16_t)(v))

/* ================================================================
 * Byte/word register access
 *
 * These macros extract or set partial registers, matching x86
 * behavior where writing AL doesn't affect bits 8-31 of EAX.
 * ================================================================ */

/** Extract low byte (al, bl, cl, dl). */
#define LO8(r)  ((uint8_t)((r) & 0xFF))
/** Extract high byte of low word (ah, bh, ch, dh). */
#define HI8(r)  ((uint8_t)(((r) >> 8) & 0xFF))
/** Extract low word (ax, bx, cx, dx). */
#define LO16(r) ((uint16_t)((r) & 0xFFFF))

/** Set low byte, preserving upper 24 bits. */
#define SET_LO8(r, v)  ((r) = ((r) & 0xFFFFFF00u) | ((uint32_t)(uint8_t)(v)))
/** Set high byte of low word, preserving other bits. */
#define SET_HI8(r, v)  ((r) = ((r) & 0xFFFF00FFu) | (((uint32_t)(uint8_t)(v)) << 8))
/** Set low word, preserving upper 16 bits. */
#define SET_LO16(r, v) ((r) = ((r) & 0xFFFF0000u) | ((uint32_t)(uint16_t)(v)))

/* ================================================================
 * Stack simulation
 *
 * For push/pop heavy prologues in the generated code.
 * ================================================================ */

/**
 * Push a 32-bit value onto the simulated stack.
 * Evaluates val BEFORE decrementing sp, matching x86 semantics
 * where push [esp+N] reads the operand before adjusting ESP.
 */
#define PUSH32(sp, val) do { \
    uint32_t _pv = (uint32_t)(val); \
    (sp) -= 4; \
    MEM32(sp) = _pv; \
} while(0)

/**
 * x86 parity flag: 1 when the low byte of the result has an EVEN number of set
 * bits (that is what PF means). Used by the x87 float-compare idiom
 * `fnstsw ax; test ah, mask; jp/jnp`, which is how all pre-SSE code branches on
 * a float comparison. Without a real parity here the branch was hardcoded and
 * every such comparison went one fixed direction.
 */
static inline int recomp_parity8(uint32_t x) {
    x &= 0xFFu; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (int)(~x & 1u);   /* 1 = even parity (PF set) */
}
#define RECOMP_PARITY8(x) recomp_parity8((uint32_t)(x))

/** Pop a 32-bit value from the simulated stack. dst is written after sp moves: `pop esp` leaves esp at the popped
 * value, not the popped value + 4 (Intel SDM, POP; sp00n 54b64ca - the round-to-int idiom
 * `push esp; push eax; ...; pop eax; pop esp` drifted the stack by 4). */
#define POP32(sp, dst) do { \
    uint32_t _pv = MEM32(sp); \
    (sp) += 4; \
    (dst) = _pv; \
} while(0)

/* ================================================================
 * Byte swap (for endian conversion if needed)
 *
 * Xbox is little-endian like x86, so these are rarely needed,
 * but some games use bswap for network byte order or data parsing.
 * ================================================================ */

static inline uint32_t BSWAP32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000u);
}

static inline uint16_t BSWAP16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

/* ================================================================
 * Indirect call dispatch
 *
 * The dispatch system resolves Xbox virtual addresses to native
 * function pointers at runtime. Three lookup sources are checked:
 *   1. Manual overrides (hand-written reimplementations)
 *   2. Generated dispatch table (auto-recompiled functions)
 *   3. Kernel thunk bridge (Xbox kernel function replacements)
 * ================================================================ */

/**
 * recomp_func_t, the type of every recompiled function, is declared with the
 * register set above: void(RECOMP_PARAMS) -- arguments and return values still
 * travel in the guest registers and on the simulated stack.
 */
#ifndef RECOMP_DISPATCH_H  /* avoid conflict with recomp_dispatch.h */

/**
 * Look up a recompiled function by its Xbox VA.
 * Returns NULL if the VA is not in the generated dispatch table.
 */
recomp_func_t recomp_lookup(uint32_t xbox_va);

/**
 * Build the flat, directly-indexed dispatch table.
 *
 * Turns recomp_lookup from a binary search over every translated function into
 * a bounds check and one indexed load -- the C form of Microsoft's
 * `jmp [table + guest_eip*8]`. Call it once at startup, before any recompiled
 * code runs.
 *
 * Entirely optional: if it is never called, or returns 0 because the allocation
 * failed, recomp_lookup keeps using the binary search and everything still
 * works. Costs 8 bytes per byte of guest code span (see
 * recomp_dispatch_flat_bytes), allocated with calloc so the untouched middle
 * stays uncommitted.
 *
 * Returns 1 if the flat table is in use, 0 if the search is.
 */
int recomp_dispatch_init(void);

/** Bytes held by the flat table, or 0 if it was never built. */
size_t recomp_dispatch_flat_bytes(void);

/**
 * Look up a kernel thunk function by its synthetic VA.
 * Kernel thunks live at 0xFE000000+ (synthetic addresses assigned
 * during kernel bridge initialization).
 * Returns NULL if the VA is not a kernel thunk.
 */
recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);

/**
 * Look up a manually overridden function by its Xbox VA.
 * Manual overrides take priority over generated code.
 * Returns NULL if no manual override exists for this VA.
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
#endif

/**
 * RECOMP_ABI_CALL - call a resolved target, optionally checking the ABI.
 *
 * ebx, esi and edi are callee-saved on x86, and the recompiler keeps them in
 * globals rather than on the host's stack. So a lifted function that never
 * reaches its own epilogue -- the usual cause is a decode that lost sync on
 * embedded data -- does not merely lose its own state, it silently corrupts
 * every caller's.
 *
 * That failure is invisible at the crash site: the caller carries on with a
 * wrong loop cursor and simply does less work. Half-Life 2's C++ static
 * initialiser walks 5,305 constructors with esi as the cursor and edi as the
 * limit; a single callee returning with those changed ended the walk early and
 * left Source with no registered interfaces, with no error anywhere.
 *
 * Build with -DRECOMP_ABI_CHECK to have every indirect call verify them and
 * name the first offenders. esp is deliberately not checked: the convention
 * decides whether the callee pops arguments, so there is no single correct
 * value -- but there is one invariant that holds under every convention:
 * the callee at least pops its own return address, so esp must come back
 * at least 4 higher than it went in. Coming back lower means the epilogue
 * never ran, which is the failure this exists to catch.
 *
 * Covers direct calls as well as indirect: tools/recomp emits every direct
 * call through this macro, which expands to a plain call when the flag is
 * off. That matters because CRT and static-initialiser paths -- where these
 * clobbers actually bite -- are almost entirely direct calls.
 * Generated code emits a direct call as a plain C call to the symbol, with no
 * macro to hook, so a direct callee that clobbers these registers is invisible
 * here. That matters more than it sounds -- CRT and static-initialiser paths
 * are almost entirely direct calls, so this found nothing at all on Half-Life
 * 2's static init, where the clobber demonstrably exists. It is the right tool
 * for vtable-dispatch-heavy code and the wrong one for early boot.
 */
#ifdef RECOMP_ABI_CHECK
void recomp_abi_violation_log(uint32_t va, uint32_t ebx0, uint32_t esi0,
                              uint32_t edi0, uint32_t esp0);
#define RECOMP_ABI_CALL(va, fn) do { \
    uint32_t _ab = g_ebx, _as = g_esi, _ad = g_edi, _ap = g_esp; \
    (fn)(RECOMP_ARGS); \
    if (g_ebx != _ab || g_esi != _as || g_edi != _ad || g_esp < _ap + 4) \
        recomp_abi_violation_log((va), _ab, _as, _ad, _ap); \
} while(0)
#elif defined(RECOMP_CALL_PROFILE)
/* Opt-in profiling: every direct call reports its target to the project, which
 * keeps the counts (a project defines recomp_call_profile_hit). Diffing the counts
 * of two situations shows which functions one of them runs and the other does not. */
void recomp_call_profile_hit(uint32_t va);
#define RECOMP_ABI_CALL(va, fn) do { recomp_call_profile_hit(va); (fn)(RECOMP_ARGS); } while(0)
#else
#define RECOMP_ABI_CALL(va, fn) (fn)(RECOMP_ARGS)
#endif

/* A guarded arm of an indirect call. When a recorded run saw a call site
 * reach only a few translated functions, the lifter compares the target
 * against each and calls the match directly (RECOMP_ABI_CALL), falling back
 * to RECOMP_ICALL_SAFE_AT for anything else -- so an unseen target is slow,
 * never wrong. Under RECOMP_ICALL_FEEDBACK the arms are counted, so a run
 * can say how often the guards held; otherwise they cost nothing. */
#ifdef RECOMP_ICALL_FEEDBACK
extern volatile uint64_t g_icall_guard_hits;
extern volatile uint64_t g_icall_guard_misses;
#define RECOMP_ICALL_GUARD_HIT()  ((void)g_icall_guard_hits++)
#define RECOMP_ICALL_GUARD_MISS() ((void)g_icall_guard_misses++)
#else
#define RECOMP_ICALL_GUARD_HIT()  ((void)0)
#define RECOMP_ICALL_GUARD_MISS() ((void)0)
#endif

/**
 * RECOMP_ICALL - Indirect call through the dispatch table.
 *
 * Looks up the Xbox VA and calls the translated function.
 * Falls back to kernel bridge for kernel thunk synthetic VAs.
 * The caller must PUSH32 the guest return address before this macro.
 * If not found, pops it back off to keep the stack balanced.
 *
 * RECOMP_ICALL_IS_CODE skips garbage VAs that come from uninitialized vtable
 * pointers. Kernel thunks at 0xFE000000+ are never blocked.
 *
 * Nothing to customize: the range comes from g_xbox_code_lo/g_xbox_code_hi,
 * which the memory layout fills in from the sections it actually mapped. This
 * used to say "change the VA range check to match your game's code range",
 * left over from a hardcoded 0x00400000 cutoff that was only ever right for
 * one title. Editing this header per project is no longer a thing.
 */
#define RECOMP_ICALL(xbox_va) do { \
    uint32_t _va = (uint32_t)(xbox_va); \
    RECOMP_ICALL_TRACE_PUT(_va); \
    /* Skip garbage VAs outside code section + kernel thunk range */ \
    if (!RECOMP_ICALL_IS_CODE(_va)) { \
        recomp_icall_not_code_log(ctx, _va); \
        g_esp += 4; eax = 0; break; \
    } \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_kernel(_va); \
    if (_fn) { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_RESOLVED); \
               RECOMP_ABI_CALL(_va, _fn); } \
    else { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_UNRESOLVED); \
           recomp_icall_fail_log(ctx, _va); g_esp += 4; eax = 0; } \
} while(0)

/**
 * RECOMP_ICALL_SAFE - Stack-safe indirect call.
 *
 * Restores g_esp to saved_esp (pre-argument value) on lookup failure,
 * preventing stdcall argument leaks on failed vtable calls.
 * Use this when the caller pushes arguments that the callee would
 * normally clean up (stdcall convention).
 */
#define RECOMP_ICALL_SAFE(xbox_va, saved_esp) do { \
    uint32_t _va = (uint32_t)(xbox_va); \
    RECOMP_ICALL_TRACE_PUT(_va); \
    if (!RECOMP_ICALL_IS_CODE(_va)) { \
        recomp_icall_not_code_log(ctx, _va); \
        g_esp = (saved_esp); eax = 0; break; \
    } \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_kernel(_va); \
    if (_fn) { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_RESOLVED); \
               RECOMP_ABI_CALL(_va, _fn); } \
    else { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_UNRESOLVED); \
           recomp_icall_fail_log(ctx, _va); g_esp = (saved_esp); eax = 0; } \
} while(0)

/* Caller cleanup: a failed call pops only its return address. */
#define RECOMP_ICALL_SAFE_CC(xbox_va, saved_esp) do { \
    (void)(saved_esp); \
    uint32_t _va = (uint32_t)(xbox_va); \
    RECOMP_ICALL_TRACE_PUT(_va); \
    if (!RECOMP_ICALL_IS_CODE(_va)) { \
        recomp_icall_not_code_log(ctx, _va); \
        g_esp += 4; eax = 0; break; \
    } \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_kernel(_va); \
    if (_fn) { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_RESOLVED); \
               RECOMP_ABI_CALL(_va, _fn); } \
    else { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_UNRESOLVED); \
           recomp_icall_fail_log(ctx, _va); g_esp += 4; eax = 0; } \
} while(0)

/**
 * RECOMP_ICALL_SAFE_AT - RECOMP_ICALL_SAFE that also names the call SITE.
 *
 * `site` is the guest address of the `call` instruction. Under
 * RECOMP_ICALL_FEEDBACK the runtime records which targets each site reaches
 * (icall_sites.dump), which is what lets the lifter guard a site with direct
 * calls on the next generation. Everything else is RECOMP_ICALL_SAFE.
 */
#define RECOMP_ICALL_SAFE_AT(xbox_va, saved_esp, site) do { \
    uint32_t _va = (uint32_t)(xbox_va); \
    RECOMP_ICALL_TRACE_PUT(_va); \
    if (!RECOMP_ICALL_IS_CODE(_va)) { \
        recomp_icall_not_code_log(ctx, _va); \
        g_esp = (saved_esp); eax = 0; break; \
    } \
    RECOMP_ICALL_OBSERVE_SITE((site), _va); \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_kernel(_va); \
    if (_fn) { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_RESOLVED); \
               RECOMP_ABI_CALL(_va, _fn); } \
    else { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_UNRESOLVED); \
           recomp_icall_fail_log(ctx, _va); g_esp = (saved_esp); eax = 0; } \
} while(0)

/* Caller cleanup: a failed call pops only its return address. */
#define RECOMP_ICALL_SAFE_AT_CC(xbox_va, saved_esp, site) do { \
    (void)(saved_esp); \
    uint32_t _va = (uint32_t)(xbox_va); \
    RECOMP_ICALL_TRACE_PUT(_va); \
    if (!RECOMP_ICALL_IS_CODE(_va)) { \
        recomp_icall_not_code_log(ctx, _va); \
        g_esp += 4; eax = 0; break; \
    } \
    RECOMP_ICALL_OBSERVE_SITE((site), _va); \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_kernel(_va); \
    if (_fn) { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_RESOLVED); \
               RECOMP_ABI_CALL(_va, _fn); } \
    else { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_UNRESOLVED); \
           recomp_icall_fail_log(ctx, _va); g_esp += 4; eax = 0; } \
} while(0)

/**
 * RECOMP_ITAIL - Indirect tail call (jmp through function pointer).
 *
 * No return address is pushed - reuses the current frame's return addr.
 * Used for tail-call optimization where the original code uses
 * jmp [reg] instead of call [reg].
 */
#define RECOMP_ITAIL(xbox_va) do { \
    uint32_t _va = (uint32_t)(xbox_va); \
    recomp_func_t _fn = recomp_lookup_manual(_va); \
    if (!_fn) _fn = recomp_lookup(_va); \
    if (!_fn) _fn = recomp_lookup_kernel(_va); \
    if (_fn) { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_RESOLVED); \
               RECOMP_ABI_CALL(_va, _fn); } \
    else { RECOMP_ICALL_OBSERVE(_va, RECOMP_ICALL_SEEN_UNRESOLVED); \
           recomp_icall_fail_log(ctx, _va); g_esp += 4; g_eax = 0; } \
} while(0)

/* ================================================================
 * Register name aliases for generated code
 *
 * Map x86 volatile register names to global variables.
 * These #defines allow the generated code to use natural register
 * names (eax, ecx, edx, esp) which the preprocessor maps to the
 * corresponding globals (g_eax, g_ecx, g_edx, g_esp).
 *
 * Only active when RECOMP_GENERATED_CODE is defined (in generated
 * .c files) to avoid polluting hand-written code.
 * ================================================================ */

#ifdef RECOMP_GENERATED_CODE
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi

/* ================================================================
 * MMX register file
 *
 * The Xbox is a Pentium III and every XDK codec leans on MMX: the WMV
 * decoder's IDCT and motion compensation are almost nothing else. Modelled the
 * same way as RecompXmm -- a union of lane views over one 64-bit register --
 * because that is what the instructions are: the same bits read as bytes,
 * words or dwords.
 *
 * mm0..mm7 alias the x87 stack on real hardware. Nothing here does, and
 * nothing needs to: a title that interleaves the two calls emms between, and
 * emms is a no-op for us. Modelling the aliasing would mean giving up the
 * separate x87 model that the FPU work depends on, to reproduce a hazard the
 * hardware exists to let software avoid.
 * ================================================================ */



static inline RecompMmx MMX_ZERO(void) { RecompMmx r; r.q = 0; return r; }

/* CVTPS2PI follows MXCSR; CVTTPS2PI truncates regardless of its rounding mode.
 * Use SSE scalar conversions to avoid touching the host x87/MMX register file.
 * Non-x86 hosts use their floating-point environment for rounding instead. */
static inline int32_t MMX_CVT_F2I(float v, int truncate)
{
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    return truncate ? _mm_cvttss_si32(_mm_set_ss(v))
                    : _mm_cvtss_si32(_mm_set_ss(v));
#else
    double rounded = truncate ? trunc((double)v) : nearbyint((double)v);
    if (!(rounded >= -2147483648.0 && rounded <= 2147483647.0))
        return (int32_t)0x80000000u;       /* integer indefinite */
    return (int32_t)rounded;
#endif
}

static inline RecompMmx MMX_FROM_PS(float lo, float hi, int truncate)
{
    RecompMmx r;
    r.d[0] = MMX_CVT_F2I(lo, truncate);
    r.d[1] = MMX_CVT_F2I(hi, truncate);
    return r;
}

/** cvtpi2ps: two signed dwords in, two singles out, into the LOW half of the
 * destination -- lanes 2 and 3 keep whatever they held. That detail is the
 * whole instruction: code that builds a float4 from two of these relies on
 * the first one surviving the second. */
static inline RecompXmm XMM_FROM_PI(RecompXmm dst, RecompMmx src)
{
    dst.f[0] = (float)src.d[0];
    dst.f[1] = (float)src.d[1];
    return dst;
}

/* Macros, not functions taking the address: the guest pointer is formed where
 * the macro expands, so generated code gets its own XBOX_PTR (base-relative,
 * pointer arithmetic -- see the end of this header) and the compiler keeps the
 * pointer's provenance; restrict on ctx then still holds across the access. */
static inline RecompMmx recomp_mmx_load(const void *p) {
    RecompMmx r;
    memcpy(&r, p, 8);
    return r;
}
static inline void recomp_mmx_store(void *p, RecompMmx v) {
    memcpy(p, &v, 8);
}
#define MMX_MEM(addr)      recomp_mmx_load((const void *)RECOMP_GPTR(addr))
#define MMX_STORE(addr, v) recomp_mmx_store((void *)RECOMP_GPTR(addr), (v))

static inline RecompMmx MMX_FROM32(uint32_t v) {
    RecompMmx r; r.q = 0; r.ud[0] = v; return r;
}

/* -- saturation helpers ---------------------------------------- */
static inline int16_t recomp_sat_i16(int32_t v) {
    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}
static inline int8_t recomp_sat_i8(int32_t v) {
    return (int8_t)(v > 127 ? 127 : (v < -128 ? -128 : v));
}
static inline uint8_t recomp_sat_u8(int32_t v) {
    return (uint8_t)(v > 255 ? 255 : (v < 0 ? 0 : v));
}

static inline uint16_t recomp_sat_u16(int32_t v) {
    return (uint16_t)(v > 65535 ? 65535 : (v < 0 ? 0 : v));
}

/* -- integer arithmetic, lane-wise, wrapping -------------------- */
#define RECOMP_MMX_BINOP(NAME, LANES, FIELD, EXPR)                      \
    static inline RecompMmx NAME##_C(RecompMmx a, RecompMmx b) {            \
        RecompMmx r; int i;                                             \
        for (i = 0; i < (LANES); i++) { (void)b; r.FIELD[i] = (EXPR); } \
        return r;                                                       \
    }

RECOMP_MMX_BINOP(MMX_PADDB, 8, b, (int8_t)((uint8_t)a.b[i] + (uint8_t)b.b[i]))
RECOMP_MMX_BINOP(MMX_PADDW, 4, w, (int16_t)((uint16_t)a.w[i] + (uint16_t)b.w[i]))
RECOMP_MMX_BINOP(MMX_PADDD, 2, d, (int32_t)((uint32_t)a.d[i] + (uint32_t)b.d[i]))
RECOMP_MMX_BINOP(MMX_PSUBB, 8, b, (int8_t)((uint8_t)a.b[i] - (uint8_t)b.b[i]))
RECOMP_MMX_BINOP(MMX_PSUBW, 4, w, (int16_t)((uint16_t)a.w[i] - (uint16_t)b.w[i]))
RECOMP_MMX_BINOP(MMX_PSUBD, 2, d, (int32_t)((uint32_t)a.d[i] - (uint32_t)b.d[i]))
RECOMP_MMX_BINOP(MMX_PADDSB, 8, b, recomp_sat_i8((int32_t)a.b[i] + b.b[i]))
RECOMP_MMX_BINOP(MMX_PADDSW, 4, w, recomp_sat_i16((int32_t)a.w[i] + b.w[i]))
RECOMP_MMX_BINOP(MMX_PSUBSB, 8, b, recomp_sat_i8((int32_t)a.b[i] - b.b[i]))
RECOMP_MMX_BINOP(MMX_PSUBSW, 4, w, recomp_sat_i16((int32_t)a.w[i] - b.w[i]))
RECOMP_MMX_BINOP(MMX_PADDUSB, 8, ub,
                 recomp_sat_u8((int32_t)a.ub[i] + b.ub[i]))
RECOMP_MMX_BINOP(MMX_PSUBUSB, 8, ub,
                 recomp_sat_u8((int32_t)a.ub[i] - b.ub[i]))
RECOMP_MMX_BINOP(MMX_PADDUSW, 4, uw,
                 recomp_sat_u16((int32_t)a.uw[i] + b.uw[i]))
RECOMP_MMX_BINOP(MMX_PSUBUSW, 4, uw,
                 recomp_sat_u16((int32_t)a.uw[i] - b.uw[i]))
RECOMP_MMX_BINOP(MMX_PMULLW, 4, w, (int16_t)((int32_t)a.w[i] * b.w[i]))
RECOMP_MMX_BINOP(MMX_PMULHW, 4, w, (int16_t)(((int32_t)a.w[i] * b.w[i]) >> 16))
RECOMP_MMX_BINOP(MMX_PAVGB, 8, ub, (uint8_t)(((int32_t)a.ub[i] + b.ub[i] + 1) >> 1))
RECOMP_MMX_BINOP(MMX_PAVGW, 4, uw, (uint16_t)(((int32_t)a.uw[i] + b.uw[i] + 1) >> 1))
RECOMP_MMX_BINOP(MMX_PMINSW, 4, w, (a.w[i] < b.w[i] ? a.w[i] : b.w[i]))
RECOMP_MMX_BINOP(MMX_PMAXSW, 4, w, (a.w[i] > b.w[i] ? a.w[i] : b.w[i]))
RECOMP_MMX_BINOP(MMX_PCMPEQB, 8, b, (int8_t)(a.b[i] == b.b[i] ? -1 : 0))
RECOMP_MMX_BINOP(MMX_PCMPEQW, 4, w, (int16_t)(a.w[i] == b.w[i] ? -1 : 0))
RECOMP_MMX_BINOP(MMX_PCMPEQD, 2, d, (int32_t)(a.d[i] == b.d[i] ? -1 : 0))
RECOMP_MMX_BINOP(MMX_PCMPGTB, 8, b, (int8_t)(a.b[i] > b.b[i] ? -1 : 0))
RECOMP_MMX_BINOP(MMX_PCMPGTW, 4, w, (int16_t)(a.w[i] > b.w[i] ? -1 : 0))
RECOMP_MMX_BINOP(MMX_PCMPGTD, 2, d, (int32_t)(a.d[i] > b.d[i] ? -1 : 0))

/* pmaddwd: multiply signed words, add adjacent pairs into dwords. */
static inline RecompMmx MMX_PMADDWD_C(RecompMmx a, RecompMmx b) {
    RecompMmx r;
    r.d[0] = (int32_t)a.w[0] * b.w[0] + (int32_t)a.w[1] * b.w[1];
    r.d[1] = (int32_t)a.w[2] * b.w[2] + (int32_t)a.w[3] * b.w[3];
    return r;
}

/* -- bitwise ---------------------------------------------------- */
static inline RecompMmx MMX_PAND(RecompMmx a, RecompMmx b)  { RecompMmx r; r.q = a.q & b.q; return r; }
static inline RecompMmx MMX_PANDN(RecompMmx a, RecompMmx b) { RecompMmx r; r.q = ~a.q & b.q; return r; }
static inline RecompMmx MMX_POR(RecompMmx a, RecompMmx b)   { RecompMmx r; r.q = a.q | b.q; return r; }
static inline RecompMmx MMX_PXOR(RecompMmx a, RecompMmx b)  { RecompMmx r; r.q = a.q ^ b.q; return r; }

/* -- shifts -----------------------------------------------------
 * A count at or past the lane width gives zero for the logical shifts and a
 * full sign fill for the arithmetic ones. The count is the whole 64-bit
 * register, not one lane, and it is unsigned -- a negative-looking count is a
 * huge one, which saturates the same way.
 */
#define RECOMP_MMX_SHIFT(NAME, LANES, FIELD, WIDTH, OP)                 \
    static inline RecompMmx NAME##_C(RecompMmx a, uint64_t cnt) {           \
        RecompMmx r; int i;                                             \
        for (i = 0; i < (LANES); i++)                                   \
            r.FIELD[i] = (cnt >= (WIDTH)) ? 0 : (OP);                   \
        return r;                                                       \
    }
RECOMP_MMX_SHIFT(MMX_PSLLW, 4, uw, 16, (uint16_t)(a.uw[i] << cnt))
RECOMP_MMX_SHIFT(MMX_PSRLW, 4, uw, 16, (uint16_t)(a.uw[i] >> cnt))
RECOMP_MMX_SHIFT(MMX_PSLLD, 2, ud, 32, (uint32_t)(a.ud[i] << cnt))
RECOMP_MMX_SHIFT(MMX_PSRLD, 2, ud, 32, (uint32_t)(a.ud[i] >> cnt))

static inline RecompMmx MMX_PSLLQ_C(RecompMmx a, uint64_t cnt) {
    RecompMmx r; r.q = (cnt >= 64) ? 0 : (a.q << cnt); return r;
}
static inline RecompMmx MMX_PSRLQ_C(RecompMmx a, uint64_t cnt) {
    RecompMmx r; r.q = (cnt >= 64) ? 0 : (a.q >> cnt); return r;
}
static inline RecompMmx MMX_PSRAW_C(RecompMmx a, uint64_t cnt) {
    RecompMmx r; int i; uint64_t c = (cnt >= 16) ? 15 : cnt;
    for (i = 0; i < 4; i++) r.w[i] = (int16_t)(a.w[i] >> c);
    return r;
}
static inline RecompMmx MMX_PSRAD_C(RecompMmx a, uint64_t cnt) {
    RecompMmx r; int i; uint64_t c = (cnt >= 32) ? 31 : cnt;
    for (i = 0; i < 2; i++) r.d[i] = (int32_t)(a.d[i] >> c);
    return r;
}

/* -- unpack / pack ---------------------------------------------- */
static inline RecompMmx MMX_PUNPCKLBW_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i;
    for (i = 0; i < 4; i++) { r.ub[i*2] = a.ub[i]; r.ub[i*2+1] = b.ub[i]; }
    return r;
}
static inline RecompMmx MMX_PUNPCKHBW_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i;
    for (i = 0; i < 4; i++) { r.ub[i*2] = a.ub[i+4]; r.ub[i*2+1] = b.ub[i+4]; }
    return r;
}
static inline RecompMmx MMX_PUNPCKLWD_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i;
    for (i = 0; i < 2; i++) { r.uw[i*2] = a.uw[i]; r.uw[i*2+1] = b.uw[i]; }
    return r;
}
static inline RecompMmx MMX_PUNPCKHWD_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i;
    for (i = 0; i < 2; i++) { r.uw[i*2] = a.uw[i+2]; r.uw[i*2+1] = b.uw[i+2]; }
    return r;
}
static inline RecompMmx MMX_PUNPCKLDQ_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; r.ud[0] = a.ud[0]; r.ud[1] = b.ud[0]; return r;
}
static inline RecompMmx MMX_PUNPCKHDQ_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; r.ud[0] = a.ud[1]; r.ud[1] = b.ud[1]; return r;
}
static inline RecompMmx MMX_PACKSSWB_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i;
    for (i = 0; i < 4; i++) r.b[i]     = recomp_sat_i8(a.w[i]);
    for (i = 0; i < 4; i++) r.b[i + 4] = recomp_sat_i8(b.w[i]);
    return r;
}
static inline RecompMmx MMX_PACKUSWB_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i;
    for (i = 0; i < 4; i++) r.ub[i]     = recomp_sat_u8(a.w[i]);
    for (i = 0; i < 4; i++) r.ub[i + 4] = recomp_sat_u8(b.w[i]);
    return r;
}
static inline RecompMmx MMX_PACKSSDW_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i;
    for (i = 0; i < 2; i++) r.w[i]     = recomp_sat_i16(a.d[i]);
    for (i = 0; i < 2; i++) r.w[i + 2] = recomp_sat_i16(b.d[i]);
    return r;
}

/* -- word insert / extract / shuffle ----------------------------- */
static inline RecompMmx MMX_PSHUFW(RecompMmx a, uint32_t imm) {
    RecompMmx r; int i;
    for (i = 0; i < 4; i++) r.uw[i] = a.uw[(imm >> (i * 2)) & 3];
    return r;
}
static inline RecompMmx MMX_PINSRW(RecompMmx a, uint32_t v, uint32_t imm) {
    RecompMmx r = a; r.uw[imm & 3] = (uint16_t)v; return r;
}
static inline uint32_t MMX_PEXTRW(RecompMmx a, uint32_t imm) {
    return (uint32_t)a.uw[imm & 3];
}
static inline uint32_t MMX_PMOVMSKB_C(RecompMmx a) {
    uint32_t m = 0; int i;
    for (i = 0; i < 8; i++) if (a.ub[i] & 0x80) m |= (1u << i);
    return m;
}
static inline RecompMmx MMX_PSADBW_C(RecompMmx a, RecompMmx b) {
    RecompMmx r; int i; uint32_t s = 0;
    for (i = 0; i < 8; i++)
        s += (uint32_t)(a.ub[i] > b.ub[i] ? a.ub[i] - b.ub[i] : b.ub[i] - a.ub[i]);
    r.q = 0; r.uw[0] = (uint16_t)s;
    return r;
}

/* -- SSE2 forms (x86 hosts) -------------------------------------
 * The forms above are the portable definition (plan section 7: Linux and
 * Android hosts compile them unchanged). Their byte and word loops cost: the
 * XMV colour conversion (Burnout 3, sub_002CD892) became 1,344 host
 * instructions for 157 guest ones, and the Titles movie decoded at 23 frames/s
 * instead of 30. On an x86 host every one of these is one SSE2 instruction on
 * the low 64 bits of an XMM register, with the same lane results: the same
 * wrap-around, saturation and rounding, and for the shifts the same rule that
 * a count at or past the lane width gives zero (logical) or the sign (arithmetic),
 * the count being the whole 64-bit value. tests/mmx_sse2 compares both forms
 * on random inputs. RECOMP_MMX_SCALAR keeps the portable forms on x86 too.
 * Open item: a NEON path for ARM64 hosts, when Android work resumes. */
#if !defined(RECOMP_MMX_SCALAR) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define RECOMP_MMX_SSE2 1
static inline __m128i recomp_mmx_v(RecompMmx a) { return _mm_loadl_epi64((const __m128i *)&a); }
static inline RecompMmx recomp_mmx_r(__m128i v) { RecompMmx r; _mm_storel_epi64((__m128i *)&r, v); return r; }
static inline __m128i recomp_mmx_cnt(uint64_t c) { return _mm_loadl_epi64((const __m128i *)&c); }
#define RECOMP_MMX_SSE2_BIN(NAME, INTRIN) \
    static inline RecompMmx NAME(RecompMmx a, RecompMmx b) { return recomp_mmx_r(INTRIN(recomp_mmx_v(a), recomp_mmx_v(b))); }
#define RECOMP_MMX_SSE2_SHIFT(NAME, INTRIN) \
    static inline RecompMmx NAME(RecompMmx a, uint64_t cnt) { return recomp_mmx_r(INTRIN(recomp_mmx_v(a), recomp_mmx_cnt(cnt))); }
RECOMP_MMX_SSE2_BIN(MMX_PADDB, _mm_add_epi8)
RECOMP_MMX_SSE2_BIN(MMX_PADDW, _mm_add_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PADDD, _mm_add_epi32)
RECOMP_MMX_SSE2_BIN(MMX_PSUBB, _mm_sub_epi8)
RECOMP_MMX_SSE2_BIN(MMX_PSUBW, _mm_sub_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PSUBD, _mm_sub_epi32)
RECOMP_MMX_SSE2_BIN(MMX_PADDSB, _mm_adds_epi8)
RECOMP_MMX_SSE2_BIN(MMX_PADDSW, _mm_adds_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PSUBSB, _mm_subs_epi8)
RECOMP_MMX_SSE2_BIN(MMX_PSUBSW, _mm_subs_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PADDUSB, _mm_adds_epu8)
RECOMP_MMX_SSE2_BIN(MMX_PSUBUSB, _mm_subs_epu8)
RECOMP_MMX_SSE2_BIN(MMX_PADDUSW, _mm_adds_epu16)
RECOMP_MMX_SSE2_BIN(MMX_PSUBUSW, _mm_subs_epu16)
RECOMP_MMX_SSE2_BIN(MMX_PMULLW, _mm_mullo_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PMULHW, _mm_mulhi_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PAVGB, _mm_avg_epu8)
RECOMP_MMX_SSE2_BIN(MMX_PAVGW, _mm_avg_epu16)
RECOMP_MMX_SSE2_BIN(MMX_PMINSW, _mm_min_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PMAXSW, _mm_max_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PCMPEQB, _mm_cmpeq_epi8)
RECOMP_MMX_SSE2_BIN(MMX_PCMPEQW, _mm_cmpeq_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PCMPEQD, _mm_cmpeq_epi32)
RECOMP_MMX_SSE2_BIN(MMX_PCMPGTB, _mm_cmpgt_epi8)
RECOMP_MMX_SSE2_BIN(MMX_PCMPGTW, _mm_cmpgt_epi16)
RECOMP_MMX_SSE2_BIN(MMX_PCMPGTD, _mm_cmpgt_epi32)
RECOMP_MMX_SSE2_BIN(MMX_PMADDWD, _mm_madd_epi16)
RECOMP_MMX_SSE2_SHIFT(MMX_PSLLW, _mm_sll_epi16)
RECOMP_MMX_SSE2_SHIFT(MMX_PSRLW, _mm_srl_epi16)
RECOMP_MMX_SSE2_SHIFT(MMX_PSLLD, _mm_sll_epi32)
RECOMP_MMX_SSE2_SHIFT(MMX_PSRLD, _mm_srl_epi32)
RECOMP_MMX_SSE2_SHIFT(MMX_PSLLQ, _mm_sll_epi64)
RECOMP_MMX_SSE2_SHIFT(MMX_PSRLQ, _mm_srl_epi64)
RECOMP_MMX_SSE2_SHIFT(MMX_PSRAW, _mm_sra_epi16)
RECOMP_MMX_SSE2_SHIFT(MMX_PSRAD, _mm_sra_epi32)
/* Unpack: interleave the two low quadwords in 128 bits; the low half is the
 * L form, the high half the H form. Pack: both operands side by side in one
 * register, then the 128-bit pack; its low half is the MMX result. */
#define RECOMP_MMX_SSE2_UNPACK(NAME, INTRIN, HIGH) \
    static inline RecompMmx NAME(RecompMmx a, RecompMmx b) { \
        __m128i v = INTRIN(recomp_mmx_v(a), recomp_mmx_v(b)); \
        return recomp_mmx_r(HIGH ? _mm_srli_si128(v, 8) : v); }
RECOMP_MMX_SSE2_UNPACK(MMX_PUNPCKLBW, _mm_unpacklo_epi8, 0)
RECOMP_MMX_SSE2_UNPACK(MMX_PUNPCKHBW, _mm_unpacklo_epi8, 1)
RECOMP_MMX_SSE2_UNPACK(MMX_PUNPCKLWD, _mm_unpacklo_epi16, 0)
RECOMP_MMX_SSE2_UNPACK(MMX_PUNPCKHWD, _mm_unpacklo_epi16, 1)
RECOMP_MMX_SSE2_UNPACK(MMX_PUNPCKLDQ, _mm_unpacklo_epi32, 0)
RECOMP_MMX_SSE2_UNPACK(MMX_PUNPCKHDQ, _mm_unpacklo_epi32, 1)
#define RECOMP_MMX_SSE2_PACK(NAME, INTRIN) \
    static inline RecompMmx NAME(RecompMmx a, RecompMmx b) { \
        return recomp_mmx_r(INTRIN(_mm_unpacklo_epi64(recomp_mmx_v(a), recomp_mmx_v(b)), _mm_setzero_si128())); }
RECOMP_MMX_SSE2_PACK(MMX_PACKSSWB, _mm_packs_epi16)
RECOMP_MMX_SSE2_PACK(MMX_PACKUSWB, _mm_packus_epi16)
RECOMP_MMX_SSE2_PACK(MMX_PACKSSDW, _mm_packs_epi32)
static inline uint32_t MMX_PMOVMSKB(RecompMmx a) { return (uint32_t)_mm_movemask_epi8(recomp_mmx_v(a)) & 0xFFu; }
static inline RecompMmx MMX_PSADBW(RecompMmx a, RecompMmx b) { return recomp_mmx_r(_mm_sad_epu8(recomp_mmx_v(a), recomp_mmx_v(b))); }
#else
#define MMX_PADDB MMX_PADDB_C
#define MMX_PADDW MMX_PADDW_C
#define MMX_PADDD MMX_PADDD_C
#define MMX_PSUBB MMX_PSUBB_C
#define MMX_PSUBW MMX_PSUBW_C
#define MMX_PSUBD MMX_PSUBD_C
#define MMX_PADDSB MMX_PADDSB_C
#define MMX_PADDSW MMX_PADDSW_C
#define MMX_PSUBSB MMX_PSUBSB_C
#define MMX_PSUBSW MMX_PSUBSW_C
#define MMX_PADDUSB MMX_PADDUSB_C
#define MMX_PSUBUSB MMX_PSUBUSB_C
#define MMX_PADDUSW MMX_PADDUSW_C
#define MMX_PSUBUSW MMX_PSUBUSW_C
#define MMX_PMULLW MMX_PMULLW_C
#define MMX_PMULHW MMX_PMULHW_C
#define MMX_PAVGB MMX_PAVGB_C
#define MMX_PAVGW MMX_PAVGW_C
#define MMX_PMINSW MMX_PMINSW_C
#define MMX_PMAXSW MMX_PMAXSW_C
#define MMX_PCMPEQB MMX_PCMPEQB_C
#define MMX_PCMPEQW MMX_PCMPEQW_C
#define MMX_PCMPEQD MMX_PCMPEQD_C
#define MMX_PCMPGTB MMX_PCMPGTB_C
#define MMX_PCMPGTW MMX_PCMPGTW_C
#define MMX_PCMPGTD MMX_PCMPGTD_C
#define MMX_PSLLW MMX_PSLLW_C
#define MMX_PSRLW MMX_PSRLW_C
#define MMX_PSLLD MMX_PSLLD_C
#define MMX_PSRLD MMX_PSRLD_C
#define MMX_PMADDWD MMX_PMADDWD_C
#define MMX_PSLLQ MMX_PSLLQ_C
#define MMX_PSRLQ MMX_PSRLQ_C
#define MMX_PSRAW MMX_PSRAW_C
#define MMX_PSRAD MMX_PSRAD_C
#define MMX_PUNPCKLBW MMX_PUNPCKLBW_C
#define MMX_PUNPCKHBW MMX_PUNPCKHBW_C
#define MMX_PUNPCKLWD MMX_PUNPCKLWD_C
#define MMX_PUNPCKHWD MMX_PUNPCKHWD_C
#define MMX_PUNPCKLDQ MMX_PUNPCKLDQ_C
#define MMX_PUNPCKHDQ MMX_PUNPCKHDQ_C
#define MMX_PACKSSWB MMX_PACKSSWB_C
#define MMX_PACKUSWB MMX_PACKUSWB_C
#define MMX_PACKSSDW MMX_PACKSSDW_C
#define MMX_PMOVMSKB MMX_PMOVMSKB_C
#define MMX_PSADBW MMX_PSADBW_C
#endif

#define xmm0 g_xmm0
#define xmm1 g_xmm1
#define xmm2 g_xmm2
#define xmm3 g_xmm3
#define xmm4 g_xmm4
#define xmm5 g_xmm5
#define xmm6 g_xmm6
#define xmm7 g_xmm7
#define mm0 g_mm0
#define mm1 g_mm1
#define mm2 g_mm2
#define mm3 g_mm3
#define mm4 g_mm4
#define mm5 g_mm5
#define mm6 g_mm6
#define mm7 g_mm7
/* ebp is NOT global - it's local in each function.
 * For __SEH_prolog/epilog, use g_seh_ebp to bridge. */
#endif

/* ================================================================
 * Forward declarations for translated functions
 *
 * These are generated by the recompiler and included per-file.
 * The recomp_funcs.h header (generated) declares all translated
 * function prototypes.
 * ================================================================ */

/* An instruction the lifter has no translation for.
 *
 * The lifter used to emit a bare "TODO: mnemonic" C comment at such a site,
 * and a comment is a no-op: the instruction vanished, the guest carried
 * on, and nothing at runtime could say the site had been reached. Wreckless
 * died inside RtlAllocateHeap because `bsf` was a comment; Half-Life 2 painted
 * its intro red because `cvtpi2ps` was. Each was found by working backwards
 * from a subsystem failure that had nothing to do with the cause.
 *
 * Now the site calls this, with the instruction text and its guest address.
 * The instruction is STILL a no-op -- this changes no behaviour -- but the run
 * says which untranslated sites it reached, how often, and with what register
 * state, and with RECOMP_UNIMPL_TRAP=1 it stops at the first one. The lifter
 * keeps the comment beside the call so `grep TODO:` over a gen tree still
 * enumerates every site whether or not it is ever reached. */
void recomp_unimpl(X86Ctx *ctx, const char *text, uint32_t va);
#define RECOMP_UNIMPL(_text, _va) recomp_unimpl(ctx, (_text), (_va))

/* ================================================================
 * Generated code: guest memory through the base parameter
 *
 * Inside a generated function guest addresses are relative to its base
 * parameter, never the global g_xbox_mem_offset. Placed last so the inline
 * helpers above keep the runtime form: they have no base in scope.
 * ================================================================ */
#ifdef RECOMP_GENERATED_CODE
/* Pointer arithmetic first, then the integer: base + a zero-extended 32-bit
 * guest address (never signed, so it stays inside the 4 GB reservation). Adding
 * to (uintptr_t)base instead lost the pointer's provenance -- every guest store
 * then "may alias" the restrict ctx and the compiler reloaded registers after
 * each one (option C, C.1-5b; G4 F4 1.75 x B). */
#undef XBOX_PTR
#define XBOX_PTR(addr) ((uintptr_t)(base + (uint32_t)(addr)))
/* Game code: plain guest accesses (C.2); the lifter makes them volatile again
 * around XDK library functions. */
#undef RECOMP_MEMQ
#define RECOMP_MEMQ

/* The pushbuffer kickoff (option C, C.2). The title writes pushbuffer words
 * with plain stores, then D3D stores DMA_PUT in the NV2A USER area
 * (0xFD800040); the executor thread reads PUT (acquire) and then the words.
 * XDK library code forms its guest pointers here, and an access to the USER
 * area gets a release fence first: on x86 a compiler barrier that keeps the
 * plain pushbuffer stores in front of the PUT store (and stops the compiler
 * from dropping one that a later store overwrites), on ARM64 a dmb. Keyed to
 * the address, not to a function, so it holds for every XDK build and for a
 * kickoff the linker inlined anywhere in library code. */
static inline uintptr_t recomp_lib_ptr(uint8_t *b, uint32_t a)
{
    if (a - 0xFD800000u < 0x100u)
        RECOMP_FENCE_STORE();
    return (uintptr_t)(b + a);
}
#define RECOMP_LIB_GPTR(addr) recomp_lib_ptr(base, (uint32_t)(addr))

/* Debug builds (RECOMP_MMIO_GUARD): game code must not touch a device range
 * with plain accesses -- C.2 makes only XDK library code volatile. Every game
 * access is checked against 0xFD000000-0xFFFFFFFF (NV2A, APU, USB, flash and
 * the rest of the hardware window); a hit is reported with the function and
 * the address. Release builds: XBOX_PTR, no cost. */
#ifdef RECOMP_MMIO_GUARD
void recomp_mmio_guard_hit(const char *fn, uint32_t addr);
static inline uintptr_t recomp_guard_ptr(uint8_t *b, uint32_t a, const char *fn)
{
    if (a >= 0xFD000000u)
        recomp_mmio_guard_hit(fn, a);
    return (uintptr_t)(b + a);
}
#define RECOMP_GAME_GPTR(addr) recomp_guard_ptr(base, (uint32_t)(addr), __func__)
#else
#define RECOMP_GAME_GPTR(addr) XBOX_PTR(addr)
#endif
#undef RECOMP_GPTR
#define RECOMP_GPTR(addr) RECOMP_GAME_GPTR(addr)
#endif /* RECOMP_GENERATED_CODE */

/* Unresolved call targets (the generated stubs_unresolved file) are silent no-ops: they
 * only consume the return address. Each stub calls this first; with
 * RECOMP_STUB_LOG=1 a run logs the stubs it actually reaches -- address and the
 * return address of the call (kernel_bridge.c; first hit, then the 10th, 100th, ...). */
void recomp_stub_hit(uint32_t va, uint32_t ret);

#endif /* RECOMP_TYPES_H */
