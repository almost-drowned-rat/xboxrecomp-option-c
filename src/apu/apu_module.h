/*
 * apu_module.h -- the interface between a port and the APU module (2026-11-17, maintainer's order: the APU core
 * as a replaceable shared library on every platform, licence option C; model: conker-live-recompiled's
 * xemu_apu.dll).
 *
 * The module (xbox_apu.dll / libxbox_apu.so) holds the MCPX APU emulation: the xemu core (apu_core.c, apu_vp.c,
 * apu_dsp.c - LGPL-2.1-or-later), the DSP56300 interpreter and GP model, AC97 (apu_aci.c) and the register trap
 * (apu_mmio_hook.c). The port's exe holds everything else, including the host audio output (apu_xaudio2.c).
 * A user may build the module from its source (the release's licenses/source/) and put it in place of the
 * shipped one - that is what this interface is for.
 *
 *   exe -> module   XR_APU_EXPORTS: the public APU API of apu.h / apu_gp.h, unchanged for the ports (apu_host.c
 *                   defines the same names in the exe and forwards them), plus the register trap (handle_mmio,
 *                   behind apu_hook_handle_mmio and g_apu_state in the exe).
 *   module -> exe   XR_APU_IMPORTS: the toolkit services the emulation calls (interrupt delivery into the
 *                   title, guest stacks / TIBs, the one-CPU gate, the logger thread, the host audio output, AC97's
 *                   page) - the exe fills an XrApuHost with them; apu_module.c defines the same names inside the
 *                   module and forwards them, so the emulation's sources do not change.
 *
 * One entry point: XR_APU_ENTRY ("xr_apu_module_open"), XrApuOpenFn. The exe passes XR_APU_ABI and its
 * XrApuHost; the module returns its XrApuModule, or NULL when the ABI differs. Any change to a list below, or to
 * the meaning of an entry, raises XR_APU_ABI.
 *
 * MIT (this file is the toolkit's own).
 */
#ifndef XR_APU_MODULE_H
#define XR_APU_MODULE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XR_APU_ABI   1u
#define XR_APU_ENTRY "xr_apu_module_open"
#if defined(_WIN32)
#define XR_APU_MODULE_FILE "xbox_apu.dll"
#else
#define XR_APU_MODULE_FILE "libxbox_apu.so"
#endif

typedef struct MCPXAPUState MCPXAPUState;
typedef struct APUMixerVoice APUMixerVoice;
typedef struct X86Ctx X86Ctx;

/* A guest function (recomp_func_t of xbox_memory_layout.h): void(X86Ctx *, uint8_t *base). */
typedef void (*xr_apu_guest_fn)(X86Ctx *, uint8_t *);
typedef void (*xr_apu_ac97_after_write_fn)(volatile uint8_t *nabm, const uint8_t *before_100, uint32_t off);

/* X(return type, name, (parameters), (arguments)) - returns a value; V(name, (parameters), (arguments)) - void. */
#define XR_APU_EXPORTS(X, V)                                                                                \
    X(MCPXAPUState *, mcpx_apu_init_standalone, (uint8_t *ram_ptr), (ram_ptr))                              \
    V(mcpx_apu_shutdown, (MCPXAPUState *d), (d))                                                            \
    X(uint64_t, mcpx_apu_mmio_read, (MCPXAPUState *d, uint64_t addr, unsigned int size), (d, addr, size))   \
    V(mcpx_apu_mmio_write, (MCPXAPUState *d, uint64_t addr, uint64_t val, unsigned int size),               \
      (d, addr, val, size))                                                                                 \
    V(mcpx_apu_play_test_tone, (MCPXAPUState *d), (d))                                                      \
    X(int, apu_mixer_alloc_voice, (void), ())                                                               \
    V(apu_mixer_free_voice, (int slot), (slot))                                                             \
    X(APUMixerVoice *, apu_mixer_get_voice, (int slot), (slot))                                             \
    X(int, apu_mixer_set_position, (int slot, uint32_t byte_offset), (slot, byte_offset))                   \
    V(apu_mixer_get_state, (int slot, uint32_t *byte_offset, int *active, int *looping),                    \
      (slot, byte_offset, active, looping))                                                                 \
    V(apu_mixer_play, (int slot, int looping), (slot, looping))                                             \
    V(apu_mixer_stop, (int slot), (slot))                                                                   \
    V(apu_gp_reset, (void), ())                                                                             \
    X(int, apu_gp_wired, (void), ())                                                                        \
    V(apu_gp_direct, (int bin, int speaker, float gain), (bin, speaker, gain))                              \
    V(apu_gp_send, (int bin, int speaker, float gain), (bin, speaker, gain))                                \
    V(apu_gp_wire_xdk_std, (void), ())                                                                      \
    V(apu_gp_eq, (int bin, int speaker, uint32_t state), (bin, speaker, state))                             \
    V(apu_gp_drive, (int bin, uint32_t state), (bin, state))                                                \
    V(apu_gp_program, (uint32_t code_scratch_off, uint32_t code_words), (code_scratch_off, code_words))     \
    X(unsigned long, apu_gp_program_steps, (void), ())                                                      \
    V(apu_gp_export, (int first, int count, uint32_t scratch_off), (first, count, scratch_off))             \
    X(int, apu_gp_export_range, (uint32_t *scratch_off, uint32_t *bytes, int *first_bin),                   \
      (scratch_off, bytes, first_bin))                                                                      \
    V(apu_gp_lfe_lowpass, (int hz), (hz))                                                                   \
    X(int, apu_gp_lfe_lowpass_hz, (void), ())                                                               \
    V(apu_gp_unmodelled, (const char *what, int bin, uint32_t state, int count, int32_t quiet),             \
      (what, bin, state, count, quiet))                                                                     \
    X(int32_t, apu_gp_unmodelled_max, (void), ())                                                           \
    X(int, apu_gp_bin_speaker, (int bin), (bin))                                                            \
    X(float, apu_gp_bin_gain, (int bin), (bin))

/* What the module calls in the exe. The log functions take a va_list (the exe's xbox_log_fprintf is variadic). */
#define XR_APU_IMPORTS(X, V)                                                                                \
    X(xr_apu_guest_fn, recomp_lookup, (uint32_t xbox_va), (xbox_va))                                        \
    X(uint32_t, xbox_GetConnectedInterrupt, (uint32_t vector), (vector))                                    \
    X(uint32_t, xbox_AllocThreadTib, (void), ())                                                            \
    X(uint32_t, xbox_ContiguousAllocatedBytes, (void), ())                                                  \
    V(xbox_diag_ctx_set, (X86Ctx *ctx), (ctx))                                                              \
    X(int, xbox_worker_stack_alloc, (void), ())                                                             \
    V(xbox_worker_stack_free, (int slot), (slot))                                                           \
    X(int, xbox_IrqlBlocksInterrupts, (void), ())                                                           \
    X(int, xbox_IrqlEnterInterrupt, (X86Ctx *ctx, int level), (ctx, level))                                 \
    V(xbox_IrqlLeaveInterrupt, (X86Ctx *ctx, int saved), (ctx, saved))                                      \
    X(int, xbox_GateEnabled, (void), ())                                                                    \
    X(int, xbox_GateTryEnter, (void), ())                                                                   \
    V(xbox_GateLeave, (void), ())                                                                           \
    V(xbox_GateNote, (uint32_t routine), (routine))                                                         \
    X(int, xbox_PhysHeapVA, (uint32_t phys, uint32_t *va), (phys, va))                                      \
    X(int, xbox_PhysOwner, (uint32_t phys), (phys))                                                         \
    X(void *, xbox_McpxWindow, (void), ())                                                                  \
    V(xbox_set_irq_line, (uint32_t vector, int level), (vector, level))                                     \
    X(volatile uint8_t *, xbox_ac97_nabm, (void), ())                                                       \
    V(xbox_ac97_host_begin, (void), ())                                                                     \
    V(xbox_ac97_host_end, (void), ())                                                                       \
    V(xbox_ac97_set_after_write, (xr_apu_ac97_after_write_fn fn), (fn))                                     \
    X(int, xbox_log_vfprintf, (FILE *stream, const char *fmt, va_list ap), (stream, fmt, ap))               \
    X(int, xbox_log_fputs, (const char *s, FILE *stream), (s, stream))                                      \
    X(int, xbox_log_fputc, (int c, FILE *stream), (c, stream))                                              \
    X(size_t, xbox_log_fwrite, (const void *p, size_t size, size_t n, FILE *stream), (p, size, n, stream))  \
    X(int, xbox_log_fflush, (FILE *stream), (stream))                                                       \
    X(int, xa2_init, (void), ())                                                                            \
    V(xa2_shutdown, (void), ())                                                                             \
    X(int, xa2_is_active, (void), ())                                                                       \
    X(int, xa2_get_buffer_size, (void), ())                                                                 \
    X(int, xa2_submit_samples, (const int16_t *samples, int num_samples), (samples, num_samples))           \
    X(int, xa2_surround, (void), ())                                                                        \
    X(int, xa2_channels, (void), ())                                                                        \
    X(int, xa2_surround_requested, (void), ())

#if !defined(_WIN32)
/* POSIX: the Win32 primitives the module uses come from the exe's platform layer (win32_compat: one object table,
 * and a thread made by CreateThread gets the signal stack the MMIO trap needs - the module's frame thread runs the
 * title's interrupt routine). On Windows the module calls the system's own. */
#include "platform/xbox_winnt.h"
#define XR_APU_IMPORTS_POSIX(X, V)                                                                          \
    X(HANDLE, CreateThread, (LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg,  \
      DWORD flags, LPDWORD tid), (sa, stack, fn, arg, flags, tid))                                          \
    X(DWORD, WaitForSingleObject, (HANDLE h, DWORD ms), (h, ms))                                            \
    X(BOOL, CloseHandle, (HANDLE h), (h))                                                                   \
    V(Sleep, (DWORD ms), (ms))                                                                              \
    V(SwitchToThread, (void), ())                                                                           \
    V(InitializeCriticalSection, (LPCRITICAL_SECTION cs), (cs))                                             \
    V(EnterCriticalSection, (LPCRITICAL_SECTION cs), (cs))                                                  \
    V(LeaveCriticalSection, (LPCRITICAL_SECTION cs), (cs))                                                  \
    V(DeleteCriticalSection, (LPCRITICAL_SECTION cs), (cs))                                                 \
    V(InitializeConditionVariable, (PCONDITION_VARIABLE cv), (cv))                                          \
    X(BOOL, SleepConditionVariableCS, (PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms), (cv, cs, ms)) \
    V(WakeConditionVariable, (PCONDITION_VARIABLE cv), (cv))                                                \
    V(WakeAllConditionVariable, (PCONDITION_VARIABLE cv), (cv))                                             \
    X(BOOL, QueryPerformanceCounter, (PLARGE_INTEGER c), (c))                                               \
    X(BOOL, QueryPerformanceFrequency, (PLARGE_INTEGER f), (f))                                             \
    X(LONG, InterlockedIncrement, (volatile LONG *a), (a))                                                  \
    X(LONG, InterlockedDecrement, (volatile LONG *a), (a))                                                  \
    X(LONG, InterlockedExchange, (volatile LONG *t, LONG v), (t, v))                                        \
    X(LONG, InterlockedCompareExchange, (volatile LONG *d, LONG x, LONG c), (d, x, c))
#endif

#define XR_APU_FIELD_X(ret, name, params, args) ret (*name) params;
#define XR_APU_FIELD_V(name, params, args)      void (*name) params;

typedef struct XrApuModule {
    uint32_t abi;                          /* XR_APU_ABI */
    const char *version;                   /* the module's build: toolkit commit (informational) */
    /* The register trap (apu_hook_handle_mmio of a static build; ctx = the PCONTEXT of the fault); NULL on a
     * host without the MMIO decoder. Called from the exe's exception / signal handler. */
    int (*handle_mmio)(void *ctx, uintptr_t fault_addr, uint32_t fault_xbox_va, int is_write);
    XR_APU_EXPORTS(XR_APU_FIELD_X, XR_APU_FIELD_V)
} XrApuModule;

typedef struct XrApuHost {
    uint32_t abi;                          /* XR_APU_ABI */
    /* The guest address space: RECOMP_GUEST_BASE (g_xbox_mem_offset) and the title image's range - read by the
     * module at open and again when the APU is initialised (set once by the exe before either). */
    const ptrdiff_t *mem_offset;
    const uint32_t *image_lo, *image_hi;
    /* struct McpxApuDebug (apu_debug.h): the exe's g_dbg, which the module fills and a port's diagnostics read
     * (Orta's apu_diag_win32.c); NULL = the module keeps its own. */
    void *dbg;
#if !defined(_WIN32)
    XR_APU_IMPORTS_POSIX(XR_APU_FIELD_X, XR_APU_FIELD_V)
#endif
    XR_APU_IMPORTS(XR_APU_FIELD_X, XR_APU_FIELD_V)
} XrApuHost;

typedef const XrApuModule *(*XrApuOpenFn)(uint32_t abi, const XrApuHost *host);

#ifdef __cplusplus
}
#endif
#endif /* XR_APU_MODULE_H */
