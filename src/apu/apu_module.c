/*
 * apu_module.c -- the module side of apu_module.h: compiled only into the APU module (xbox_apu.dll /
 * libxbox_apu.so), next to the emulation's own sources.
 *
 * - xr_apu_module_open: checks the ABI, keeps the exe's XrApuHost, hands back the module's XrApuModule.
 * - Every toolkit service the emulation calls (XR_APU_IMPORTS; on POSIX also XR_APU_IMPORTS_POSIX) is defined
 *   here under its own name and forwarded to the exe, so the emulation's sources are the same as in a static
 *   build. The guest address space (g_xbox_mem_offset, the image range) is copied from the exe.
 * - mcpx_apu_init_standalone also sets the module's g_apu_state (the register trap reads it), which a port's
 *   main.c assigns from the result in a static build.
 *
 * MIT (the toolkit's own file).
 */
#include "apu_module.h"
#include "apu.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#define XR_APU_EXPORT __declspec(dllexport)
#else
#define XR_APU_EXPORT __attribute__((visibility("default")))
#endif

#if defined(XR_APU_HAVE_BUILD_INFO)
#include "xr_build_info.h"                          /* platform's generated header: XR_TOOLKIT_COMMIT */
#define XR_APU_VERSION XR_TOOLKIT_COMMIT
#else
#define XR_APU_VERSION "unknown"
#endif

static XrApuHost s_host;
static XrApuModule s_mod;

/* g_dbg of apu_state.h: the exe's when it gives one (XrApuHost.dbg), else this one */
#include "apu_debug.h"
static struct McpxApuDebug s_dbg_own;
struct McpxApuDebug *xr_apu_dbg = &s_dbg_own;

/* The guest address space, as xbox_memory_layout.h names it (the exe owns the real ones). */
ptrdiff_t g_xbox_mem_offset;
uint32_t  g_xbox_image_lo, g_xbox_image_hi;

static void copy_layout(void)
{
    if (s_host.mem_offset)
        g_xbox_mem_offset = *s_host.mem_offset;
    if (s_host.image_lo)
        g_xbox_image_lo = *s_host.image_lo;
    if (s_host.image_hi)
        g_xbox_image_hi = *s_host.image_hi;
}

/* ── the exe's services, under their own names ───────────────────────────────────────────────────────────── */

#define XR_FWD_X(ret, name, params, args) ret name params { return s_host.name args; }
#define XR_FWD_V(name, params, args)      void name params { s_host.name args; }
XR_APU_IMPORTS(XR_FWD_X, XR_FWD_V)
#if !defined(_WIN32)
XR_APU_IMPORTS_POSIX(XR_FWD_X, XR_FWD_V)
#endif

/* The variadic log calls of log_async.h (the hot-path logging header is forced into every module source). */
int xbox_log_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = s_host.xbox_log_vfprintf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int xbox_log_printf(const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = s_host.xbox_log_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int xbox_log_puts(const char *s)
{
    int r = s_host.xbox_log_fputs(s, stdout);
    return r < 0 ? r : s_host.xbox_log_fputc('\n', stdout);
}

int xbox_log_putchar(int c)
{
    return s_host.xbox_log_fputc(c, stdout);
}

/* ── the module's API ──────────────────────────────────────────────────────────────────────────────────── */

extern MCPXAPUState *g_apu_state;                   /* apu_mmio_hook.c */

static MCPXAPUState *mod_init(uint8_t *ram_ptr)
{
    copy_layout();                                  /* the exe has laid out guest memory by now */
    g_apu_state = mcpx_apu_init_standalone(ram_ptr);
    return g_apu_state;
}

#include "platform/xbox_winnt.h"
#include "platform/mmio_decode.h"
#if defined(MMIO_DECODE_AVAILABLE)
extern bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr, uint32_t fault_xbox_va, int is_write);
static int mod_hook(void *ctx, uintptr_t fault_addr, uint32_t fault_xbox_va, int is_write)
{
    return apu_hook_handle_mmio((PCONTEXT)ctx, fault_addr, fault_xbox_va, is_write);
}
#endif

#include "apu_gp.h"

XR_APU_EXPORT const XrApuModule *xr_apu_module_open(uint32_t abi, const XrApuHost *host)
{
    if (abi != XR_APU_ABI || !host || host->abi != XR_APU_ABI)
        return NULL;
    s_host = *host;
    if (host->dbg)
        xr_apu_dbg = (struct McpxApuDebug *)host->dbg;
    copy_layout();
    s_mod.abi = XR_APU_ABI;
    s_mod.version = XR_APU_VERSION;
#define XR_SET_X(ret, name, params, args) s_mod.name = (ret (*) params)(void (*)(void))name;
#define XR_SET_V(name, params, args)      s_mod.name = (void (*) params)(void (*)(void))name;
    XR_APU_EXPORTS(XR_SET_X, XR_SET_V)
    s_mod.mcpx_apu_init_standalone = mod_init;
#if defined(MMIO_DECODE_AVAILABLE)
    s_mod.handle_mmio = mod_hook;
#else
    s_mod.handle_mmio = NULL;                       /* no register trap on this host */
#endif
    return &s_mod;
}
