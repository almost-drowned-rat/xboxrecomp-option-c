/*
 * apu_host.c -- the exe side of apu_module.h (XBOXRECOMP_APU_SHARED, the default): loads the APU module and
 * keeps the public APU API of apu.h / apu_gp.h under its own names, so a port's code (main.c's
 * mcpx_apu_init_standalone / g_apu_state, its host's apu_hook_handle_mmio, a title's apu_gp_* wiring) is the
 * same as in a static build.
 *
 * Where the module is looked for, first hit wins (the path is logged):
 *   1. RECOMP_APU_MODULE=<file>        a module of the user's choice
 *   2. the running exe's folder         a release: data/ next to data/<game> (the launcher ships it there - the
 *                                       release layout, only the launcher on top; 2026-11-23); a development
 *                                       build: CMake copies the module next to the exe
 *   3. the install root (host_paths.h)  where releases before 2026-11-23 put it (the launcher now removes that copy)
 *   4. Android: the bare name           the APK's lib folder
 * No module, or one with another ABI: one line in the log and no APU - mcpx_apu_init_standalone returns NULL
 * ("MCPX APU: FAILED" in the port's log), the register trap declines, the title runs without sound emulation.
 *
 * MIT (the toolkit's own file).
 */
#include "apu_module.h"
#include "apu.h"
#include "apu_gp.h"
#include "apu_xaudio2.h"
#include "platform/xbox_winnt.h"
#include "platform/host_paths.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void xbox_ApuMmioUntrap(void);       /* kernel/xbox_memory_layout.h: no module -> the register span plain */

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

/* The exe's services the module calls (their definitions are elsewhere in the toolkit and in the port). */
#define XR_DECL_X(ret, name, params, args) extern ret name params;
#define XR_DECL_V(name, params, args)      extern void name params;
XR_APU_IMPORTS(XR_DECL_X, XR_DECL_V)
extern ptrdiff_t g_xbox_mem_offset;
extern uint32_t  g_xbox_image_lo, g_xbox_image_hi;

MCPXAPUState *g_apu_state = NULL;                  /* what the port's main.c assigns; the module keeps its own */

/* The APU's debug state (apu_debug.h): here in the exe, filled by the module (XrApuHost.dbg), read by a port's
 * diagnostics (Orta's apu_diag_win32.c) exactly as in a static build. */
#include "apu_debug.h"
struct McpxApuDebug g_dbg;

static const XrApuModule *s_mod;
static int s_tried;
static char s_path[1024];

static int try_open(const char *path)
{
    XrApuOpenFn open_fn;
    static XrApuHost host;
    const char *why;
#if defined(_WIN32)
    HMODULE h = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h)
        return 0;
    open_fn = (XrApuOpenFn)(void (*)(void))GetProcAddress(h, XR_APU_ENTRY);
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h)
        return 0;
    open_fn = (XrApuOpenFn)(void (*)(void))dlsym(h, XR_APU_ENTRY);
#endif
    memset(&host, 0, sizeof host);
    host.abi = XR_APU_ABI;
    host.mem_offset = &g_xbox_mem_offset;
    host.image_lo = &g_xbox_image_lo;
    host.image_hi = &g_xbox_image_hi;
    host.dbg = &g_dbg;
#define XR_HOST_SET(ret, name, params, args) host.name = name;
#define XR_HOST_SET_V(name, params, args)    host.name = name;
#if !defined(_WIN32)
    XR_APU_IMPORTS_POSIX(XR_HOST_SET, XR_HOST_SET_V)
#endif
    XR_APU_IMPORTS(XR_HOST_SET, XR_HOST_SET_V)
    s_mod = open_fn ? open_fn(XR_APU_ABI, &host) : NULL;
    if (s_mod) {
        snprintf(s_path, sizeof s_path, "%s", path);
        fprintf(stderr, "[APU] module %s (ABI %u, build %s)\n", path, XR_APU_ABI, s_mod->version);
        return 1;
    }
    why = open_fn ? "another ABI" : "no " XR_APU_ENTRY;
    fprintf(stderr, "[APU] module %s not used: %s (this build needs ABI %u)\n", path, why, XR_APU_ABI);
#if defined(_WIN32)
    FreeLibrary(h);
#else
    dlclose(h);
#endif
    return -1;                                     /* found but unusable: stop looking */
}

static int exe_dir(char *out, size_t n)
{
#if defined(_WIN32)
    DWORD k = GetModuleFileNameA(NULL, out, (DWORD)n);
    if (!k || k >= n)
        return 0;
#else
    ssize_t k = readlink("/proc/self/exe", out, n - 1);
    if (k <= 0)
        return 0;
    out[k] = 0;
#endif
    {
        char *s = strrchr(out, '/'), *b = strrchr(out, '\\');
        if (b > s)
            s = b;
        if (!s)
            return 0;
        s[1] = 0;
    }
    return 1;
}

static const XrApuModule *mod(void)
{
    char p[1024];
    const char *e;
    int r = 0;
    if (s_mod || s_tried)
        return s_mod;
    s_tried = 1;
    e = getenv("RECOMP_APU_MODULE");
    if (e && *e) {
        r = try_open(e);
        if (!r) {
            fprintf(stderr, "[APU] RECOMP_APU_MODULE=%s does not load\n", e);
            xbox_ApuMmioUntrap();                  /* no sound emulation, but no fault on the title's APU access */
        }
        return s_mod;                              /* the user's choice or nothing */
    }
    if (!r && exe_dir(p, sizeof p) && strlen(p) + sizeof XR_APU_MODULE_FILE < sizeof p) {
        strcat(p, XR_APU_MODULE_FILE);
        r = try_open(p);
    }
    if (!r && snprintf(p, sizeof p, "%s%s", xbox_host_root(), XR_APU_MODULE_FILE) < (int)sizeof p)
        r = try_open(p);
#if defined(__ANDROID__)
    if (!r)
        r = try_open(XR_APU_MODULE_FILE);
#endif
    if (!r) {
        fprintf(stderr, "[APU] no %s (next to the exe, install root, RECOMP_APU_MODULE) - no sound emulation\n",
                XR_APU_MODULE_FILE);
        xbox_ApuMmioUntrap();                      /* (2026-11-24: without this the first APU access faulted) */
    }
    return s_mod;
}

/* ── the public API, forwarded ─────────────────────────────────────────────────────────────────────────── */

#define XR_API_X(ret, name, params, args) ret name params { const XrApuModule *m = mod(); \
                                                            return m ? m->name args : (ret)0; }
#define XR_API_V(name, params, args)      void name params { const XrApuModule *m = mod(); \
                                                             if (m) m->name args; }
XR_APU_EXPORTS(XR_API_X, XR_API_V)

/* From the exe's exception / signal handler: never loads the module there (it is loaded by the port's APU
 * start-up, mcpx_apu_init_standalone, long before the first trapped register access). */
bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr, uint32_t fault_xbox_va, int is_write)
{
    const XrApuModule *m = s_mod;
    return m && m->handle_mmio && m->handle_mmio((void *)ctx, fault_addr, fault_xbox_va, is_write);
}

/* Which module is in use ("" = none yet): for a port's log or settings page. */
const char *xr_apu_module_path(void)
{
    return s_path;
}
